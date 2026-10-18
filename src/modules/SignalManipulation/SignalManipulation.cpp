#include "SignalManipulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
#include <numbers>
#include <utility>
#include <vector>

#include <fftw3.h>

#include <Module/Decoder/RSC/BCJR/Seq_generic/Decoder_RSC_BCJR_seq_generic_std.hpp>
#include <Module/Decoder/Turbo/Decoder_turbo_std.hpp>
#include <Module/Encoder/RSC/Encoder_RSC_generic_sys.hpp>
#include <Module/Encoder/Turbo/Encoder_turbo.hpp>
#include <Module/Interleaver/Interleaver.hpp>
#include <Tools/Interleaver/LTE/Interleaver_core_LTE.hpp>


namespace Radiocom::SignalManipulation
{
namespace
{

constexpr size_t bitsPerByte = 8;
constexpr size_t bitsPerSymbol = 2;
constexpr double qpskAmplitude = 0.707;

constexpr std::array<Sample_t, 4> qpskConstellation = {
    Sample_t(qpskAmplitude, qpskAmplitude),
    Sample_t(-qpskAmplitude, qpskAmplitude),
    Sample_t(qpskAmplitude, -qpskAmplitude),
    Sample_t(-qpskAmplitude, -qpskAmplitude),
};

// Сетка LTE для полосы 3 МГц: 15 RB по 12 поднесущих, слот из 7 символов с обычным CP (TS 36.211, 6.2.3, 6.12)
constexpr size_t fftSize = 256;
constexpr size_t subcarrierCount = 180;
constexpr size_t symbolsPerSlot = 7;

constexpr size_t cyclicPrefixLength(size_t symbol)
{
    return symbol == 0 ? 20 : 18;
}

// Индекс отсчёта слота, с которого начинается циклический префикс символа
constexpr size_t symbolOffset(size_t symbol)
{
    size_t offset = 0;
    for (size_t previous = 0; previous < symbol; ++previous)
    {
        offset += cyclicPrefixLength(previous) + fftSize;
    }
    return offset;
}

constexpr size_t slotLength = symbolOffset(symbolsPerSlot);

// Поднесущие -90..-1 лежат в нижней половине строки сетки, 1..90 — в верхней, DC пропущена (TS 36.211, 6.12)
constexpr size_t frequencyBin(size_t subcarrier)
{
    constexpr size_t half = subcarrierCount / 2;
    return subcarrier < half ? fftSize - half + subcarrier : subcarrier - half + 1;
}

// PSS: последний символ слота 0, 62 поднесущие вокруг DC: k = n - 31 + 90 (TS 36.211, 6.11.1.2)
constexpr size_t syncSymbol = symbolsPerSlot - 1;
constexpr size_t syncLength = 62;
constexpr size_t syncFirstSubcarrier = subcarrierCount / 2 - syncLength / 2;
constexpr size_t zadoffChuLength = 63;
constexpr size_t zadoffChuRoot = 25;

constexpr size_t offsetEstimationPasses = 1;
constexpr double searchRangeHz = 15'000.0;
constexpr double hypothesisStepHz = 3'000.0;

using ResourceGrid_t = std::array<std::array<Sample_t, subcarrierCount>, symbolsPerSlot>;

// Последовательность Задова — Чу, как PSS в LTE (TS 36.211, 6.11.1.1)
Signal_t generateSyncSequence()
{
    Signal_t sequence(syncLength);
    for (size_t index = 0; index < syncLength; ++index)
    {
        // Показатель exp(-j*pi*e / 63) повторяется с периодом 2 * 63, поэтому e берётся по модулю
        const size_t product = index < syncLength / 2 ? index * (index + 1) : (index + 1) * (index + 2);
        const size_t exponent = zadoffChuRoot * product % (2 * zadoffChuLength);
        sequence[index] = std::polar(1.0, -std::numbers::pi * double(exponent) / double(zadoffChuLength));
    }
    return sequence;
}

void mapSyncSignal(ResourceGrid_t& grid)
{
    const Signal_t sequence = generateSyncSequence();
    std::copy(sequence.begin(), sequence.end(), grid[syncSymbol].begin() + std::ptrdiff_t(syncFirstSubcarrier));
}

// CRS порта 0 (TS 36.211, 6.10.1): символы 0 и 4 слота, каждая 6-я поднесущая, во втором символе со сдвигом 3
constexpr size_t cellId = 0;
constexpr size_t slotNumber = 0;
constexpr size_t maxResourceBlocks = 110;
constexpr size_t resourceBlocks = subcarrierCount / 12;
constexpr size_t referenceSpacing = 6;
constexpr size_t referencesPerSymbol = 2 * resourceBlocks;
constexpr std::array<size_t, 2> referenceSymbols = {0, symbolsPerSlot - 3};
constexpr size_t goldOffset = 1600;

constexpr size_t referenceShift(size_t symbol)
{
    const size_t shift = symbol == 0 ? 0 : 3;
    return (shift + cellId % referenceSpacing) % referenceSpacing;
}

constexpr bool isReferenceElement(size_t symbol, size_t subcarrier)
{
    return (symbol == referenceSymbols[0] || symbol == referenceSymbols[1]) &&
           subcarrier % referenceSpacing == referenceShift(symbol);
}

// Ресурсный элемент несёт данные, если он не занят синхросигналом и опорными сигналами
constexpr bool isDataElement(size_t symbol, size_t subcarrier)
{
    return symbol != syncSymbol && !isReferenceElement(symbol, subcarrier);
}

// Последовательность Голда длиной 31 (TS 36.211, 7.2)
std::vector<uint8_t> generateGoldSequence(uint32_t initialValue, size_t length)
{
    const size_t total = goldOffset + length;
    std::vector<uint8_t> first(total + 31, 0);
    std::vector<uint8_t> second(total + 31, 0);
    first[0] = 1;
    for (size_t bit = 0; bit < 31; ++bit)
    {
        second[bit] = (initialValue >> bit) & 1;
    }
    for (size_t index = 0; index < total; ++index)
    {
        first[index + 31] = (first[index + 3] + first[index]) & 1;
        second[index + 31] = (second[index + 3] + second[index + 2] + second[index + 1] + second[index]) & 1;
    }

    std::vector<uint8_t> sequence(length);
    for (size_t index = 0; index < length; ++index)
    {
        sequence[index] = (first[index + goldOffset] + second[index + goldOffset]) & 1;
    }
    return sequence;
}

// Пилоты символа слота (TS 36.211, 6.10.1.1): r(m'), m' = m + 110 - 15
Signal_t generateReferenceSequence(size_t symbol)
{
    constexpr size_t normalCyclicPrefix = 1;
    const uint32_t initialValue = uint32_t((1U << 10) * (7 * (slotNumber + 1) + symbol + 1) * (2 * cellId + 1) +
                                           2 * cellId + normalCyclicPrefix);
    const auto gold = generateGoldSequence(initialValue, 4 * maxResourceBlocks);

    Signal_t sequence(referencesPerSymbol);
    const size_t firstIndex = maxResourceBlocks - resourceBlocks;
    for (size_t index = 0; index < referencesPerSymbol; ++index)
    {
        const size_t m = firstIndex + index;
        sequence[index] = std::sqrt(0.5) * Sample_t(1.0 - 2.0 * gold[2 * m], 1.0 - 2.0 * gold[2 * m + 1]);
    }
    return sequence;
}

void mapReferenceSignals(ResourceGrid_t& grid)
{
    for (const size_t symbol : referenceSymbols)
    {
        const Signal_t sequence = generateReferenceSequence(symbol);
        for (size_t index = 0; index < referencesPerSymbol; ++index)
        {
            grid[symbol][referenceSpacing * index + referenceShift(symbol)] = sequence[index];
        }
    }
}

// Линейная интерполяция по частоте между пилотами, за крайними пилотами — линейная экстраполяция
std::array<Sample_t, subcarrierCount> interpolateInFrequency(std::span<const size_t> positions,
                                                             std::span<const Sample_t> gains)
{
    std::array<Sample_t, subcarrierCount> channel;
    for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
    {
        const auto next = std::upper_bound(positions.begin(), positions.end(), subcarrier);
        const size_t upper = std::clamp(size_t(next - positions.begin()), size_t(1), positions.size() - 1);
        const size_t lower = upper - 1;
        const double fraction = (double(subcarrier) - double(positions[lower])) /
                                (double(positions[upper]) - double(positions[lower]));
        channel[subcarrier] = gains[lower] + (gains[upper] - gains[lower]) * fraction;
    }
    return channel;
}

// Оценка канала по CRS: отношение принятого к известному в пилотах, интерполяция по частоте, затем по времени
ResourceGrid_t estimateChannel(const ResourceGrid_t& received)
{
    ResourceGrid_t reference{};
    mapReferenceSignals(reference);

    std::array<std::array<Sample_t, subcarrierCount>, referenceSymbols.size()> estimates;
    for (size_t index = 0; index < referenceSymbols.size(); ++index)
    {
        const size_t symbol = referenceSymbols[index];
        std::vector<size_t> positions;
        Signal_t gains;
        for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
        {
            if (isReferenceElement(symbol, subcarrier))
            {
                positions.push_back(subcarrier);
                gains.push_back(received[symbol][subcarrier] / reference[symbol][subcarrier]);
            }
        }
        estimates[index] = interpolateInFrequency(positions, gains);
    }

    ResourceGrid_t channel{};
    const double symbolDistance = double(referenceSymbols[1]) - double(referenceSymbols[0]);
    for (size_t symbol = 0; symbol < symbolsPerSlot; ++symbol)
    {
        const double weight = (double(symbol) - double(referenceSymbols[0])) / symbolDistance;
        for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
        {
            channel[symbol][subcarrier] =
                estimates[0][subcarrier] + (estimates[1][subcarrier] - estimates[0][subcarrier]) * weight;
        }
    }
    return channel;
}

// Канальное кодирование транспортного блока

using Bits_t = std::vector<uint8_t>;

// Проектное соглашение: байты нагрузки раскладываются в биты старшим битом вперёд (в стандарте для этого нет страницы)
Bits_t bytesToBits(std::span<const uint8_t> bytes)
{
    Bits_t bits;
    bits.reserve(bytes.size() * bitsPerByte);
    for (const uint8_t byte : bytes)
    {
        for (size_t shift = bitsPerByte; shift > 0; --shift)
        {
            bits.push_back((byte >> (shift - 1)) & 1U);
        }
    }
    return bits;
}

Bytes_t bitsToBytes(std::span<const uint8_t> bits)
{
    Bytes_t bytes(bits.size() / bitsPerByte, 0);
    for (size_t index = 0; index < bytes.size() * bitsPerByte; ++index)
    {
        bytes[index / bitsPerByte] |= uint8_t(bits[index] << (bitsPerByte - 1 - index % bitsPerByte));
    }
    return bytes;
}

// CRC24A (TS 36.212 V15.2.1, 5.1.1):
// g(D) = D^24 + D^23 + D^18 + D^17 + D^14 + D^11 + D^10 + D^7 + D^6 + D^5 + D^4 + D^3 + D + 1, здесь без члена D^24
constexpr size_t crcLength = 24;
constexpr uint32_t crcPolynomial = 0x864CFB;
constexpr uint32_t crcMask = (1U << crcLength) - 1;

// Остаток от деления bits * D^24 на g(D); для блока с присоединённой CRC остаток равен нулю (5.1.1)
uint32_t calculateCrc(std::span<const uint8_t> bits)
{
    uint32_t remainder = 0;
    for (const uint8_t bit : bits)
    {
        const uint32_t feedback = ((remainder >> (crcLength - 1)) & 1U) ^ bit;
        remainder = (remainder << 1) & crcMask;
        if (feedback != 0)
        {
            remainder ^= crcPolynomial;
        }
    }
    return remainder;
}

// Биты чётности идут после информационных, p_0 — старший бит остатка, коэффициент при D^23 (5.1.1)
Bits_t attachCrc(std::span<const uint8_t> bits)
{
    const uint32_t remainder = calculateCrc(bits);
    Bits_t block(bits.begin(), bits.end());
    for (size_t index = 0; index < crcLength; ++index)
    {
        block.push_back((remainder >> (crcLength - 1 - index)) & 1U);
    }
    return block;
}

// Турбокод (TS 36.212 V15.2.1, 5.1.3.2): два составных кодера с передаточной функцией [1, g1(D)/g0(D)],
// g0(D) = 1 + D^2 + D^3, g1(D) = 1 + D + D^3 (5.1.3.2.1), восемь состояний, перемежитель QPP (5.1.3.2.3)
constexpr std::array<int, 2> turboPolynomials = {013, 015};
constexpr int turboMemory = 3;
constexpr size_t turboStreamCount = 3;
// Каждый из потоков d(0), d(1), d(2) длиннее блока на 4 бита завершения решётки (5.1.3.2.2)
constexpr size_t turboTailLength = 4;
// Число итераций декодера выбрано в проекте
constexpr int turboIterations = 6;

class TurboCodec
{
public:
    // Размер блока должен быть в табл. 5.1.3-3 (TS 36.212), иначе фабрика возвращает nullptr
    static std::unique_ptr<TurboCodec> Create(size_t blockSize)
    {
        try
        {
            return std::unique_ptr<TurboCodec>(new TurboCodec(int(blockSize)));
        }
        catch (const std::exception&)
        {
            return nullptr;
        }
    }

    TurboCodec(const TurboCodec& other) = delete;
    TurboCodec(TurboCodec&& other) = delete;
    TurboCodec& operator=(const TurboCodec& other) = delete;
    TurboCodec& operator=(TurboCodec&& other) = delete;
    ~TurboCodec() = default;

    size_t GetStreamLength() const { return size_t(m_blockSize) + turboTailLength; }

    // Потоки d(0), d(1), d(2) подряд: [d(0) | d(1) | d(2)]
    Bits_t Encode(std::span<const uint8_t> block)
    {
        const std::vector<int> information(block.begin(), block.end());
        m_encoder.encode(information, m_codeword);

        Bits_t streams(m_layout.size());
        for (size_t position = 0; position < streams.size(); ++position)
        {
            streams[position] = uint8_t(m_codeword[m_layout[position]]);
        }
        return streams;
    }

    // Мягкие значения бит в том же порядке, что результат Encode; положительное значение — бит 0
    std::optional<Bits_t> Decode(std::span<const float> streams)
    {
        for (size_t position = 0; position < streams.size(); ++position)
        {
            m_llr[m_layout[position]] = streams[position];
        }
        m_decoder.decode_siho(m_llr, m_decoded);
        return Bits_t(m_decoded.begin(), m_decoded.end());
    }

private:
    explicit TurboCodec(int blockSize)
        : m_blockSize(blockSize)
        , m_codewordLength(3 * blockSize + 4 * turboMemory)
        , m_encoderRsc(blockSize,
                       2 * (blockSize + turboMemory),
                       true,
                       std::vector<int>(turboPolynomials.begin(), turboPolynomials.end()))
        , m_core(blockSize)
        , m_interleaverBit(m_core)
        , m_interleaverLlr(m_core)
        , m_encoder(blockSize, m_codewordLength, m_encoderRsc, m_encoderRsc, m_interleaverBit)
        , m_decoderRsc(blockSize, m_encoderRsc.get_trellis(), true)
        , m_decoder(blockSize, m_codewordLength, turboIterations, m_decoderRsc, m_decoderRsc, m_interleaverLlr, true)
        , m_codeword(size_t(m_codewordLength))
        , m_llr(size_t(m_codewordLength))
        , m_decoded(size_t(blockSize))
        , m_layout(buildLayout(size_t(blockSize)))
    {
    }

    // AFF3CT выдаёт слово [x | хвост x | z | хвост z | хвост x' | z' | хвост z'], где x — информационные биты, z и z' —
    // чётность первого и второго кодера, x' — информационные биты второго кодера; хвосты по три бита.
    // Здесь для каждой позиции [d(0) | d(1) | d(2)] найден индекс в слове AFF3CT по формулам 5.1.3.2.2
    static std::vector<size_t> buildLayout(size_t blockSize)
    {
        const size_t tail = size_t(turboMemory);
        const size_t systematic = 0;
        const size_t tailSystematic = blockSize;
        const size_t parity = blockSize + tail;
        const size_t tailParity = 2 * blockSize + tail;
        const size_t tailSystematicSecond = 2 * blockSize + 2 * tail;
        const size_t paritySecond = 2 * blockSize + 3 * tail;
        const size_t tailParitySecond = 3 * blockSize + 3 * tail;

        std::vector<size_t> layout;
        layout.reserve(turboStreamCount * (blockSize + turboTailLength));
        for (size_t index = 0; index < blockSize; ++index)
        {
            layout.push_back(systematic + index);
        }
        layout.insert(layout.end(),
                      {tailSystematic, tailParity + 1, tailSystematicSecond, tailParitySecond + 1});
        for (size_t index = 0; index < blockSize; ++index)
        {
            layout.push_back(parity + index);
        }
        layout.insert(layout.end(),
                      {tailParity, tailSystematic + 2, tailParitySecond, tailSystematicSecond + 2});
        for (size_t index = 0; index < blockSize; ++index)
        {
            layout.push_back(paritySecond + index);
        }
        layout.insert(layout.end(),
                      {tailSystematic + 1, tailParity + 2, tailSystematicSecond + 1, tailParitySecond + 2});
        return layout;
    }

    int m_blockSize;
    int m_codewordLength;
    aff3ct::module::Encoder_RSC_generic_sys<int> m_encoderRsc;
    aff3ct::tools::Interleaver_core_LTE<> m_core;
    aff3ct::module::Interleaver<int> m_interleaverBit;
    aff3ct::module::Interleaver<float> m_interleaverLlr;
    aff3ct::module::Encoder_turbo<int> m_encoder;
    aff3ct::module::Decoder_RSC_BCJR_seq_generic_std<int, float> m_decoderRsc;
    aff3ct::module::Decoder_turbo_std<int, float> m_decoder;
    std::vector<int> m_codeword;
    std::vector<float> m_llr;
    std::vector<int> m_decoded;
    std::vector<size_t> m_layout;
};

// Согласование скорости турбокода (TS 36.212 V15.2.1, 5.1.4.1)
// C_subblock (5.1.4.1.1, шаг 1)
constexpr size_t subblockColumns = 32;
// Перестановка столбцов, табл. 5.1.4-1
constexpr std::array<size_t, subblockColumns> columnPermutation = {0, 16, 8,  24, 4, 20, 12, 28, 2, 18, 10,
                                                                    26, 6, 22, 14, 30, 1,  17, 9,  25, 5, 21,
                                                                    13, 29, 3,  19, 11, 27, 7,  23, 15, 31};
// rv_idx первой передачи (5.1.4.1.2)
constexpr size_t redundancyVersion = 0;
constexpr size_t nullPosition = SIZE_MAX;

// Для каждого из outputLength выходных бит e_k возвращает его позицию в [d(0) | d(1) | d(2)].
// Размер кольцевого буфера N_cb принят равным K_w: ограничения по буферу приёмника (N_IR, 5.1.4.1.2) в проекте нет
std::vector<size_t> buildRateMatchingMap(size_t streamLength, size_t outputLength)
{
    // R_subblock — наименьшее число строк, при котором D <= R * C (5.1.4.1.1, шаг 2)
    const size_t rows = (streamLength + subblockColumns - 1) / subblockColumns;
    const size_t interleavedLength = rows * subblockColumns;
    // N_D бит <NULL> в начале, затем d_k (шаг 3)
    const size_t dummyCount = interleavedLength - streamLength;
    const auto streamIndex = [&](size_t matrixPosition, size_t stream)
    { return matrixPosition < dummyCount ? nullPosition : stream * streamLength + matrixPosition - dummyCount; };

    // Кольцевой буфер w длиной K_w = 3 * K_Pi (5.1.4.1.2)
    std::vector<size_t> circularBuffer(3 * interleavedLength);
    for (size_t column = 0; column < subblockColumns; ++column)
    {
        for (size_t row = 0; row < rows; ++row)
        {
            // Для d(0) и d(1): матрица записана по строкам, столбцы переставлены, чтение по столбцам (шаги 4 и 5)
            const size_t position = column * rows + row;
            const size_t source = row * subblockColumns + columnPermutation[column];
            circularBuffer[position] = streamIndex(source, 0);
            circularBuffer[interleavedLength + 2 * position] = streamIndex(source, 1);
        }
    }
    for (size_t position = 0; position < interleavedLength; ++position)
    {
        // Для d(2): v_k = y_pi(k), pi(k) = (P(floor(k / R)) + C * (k mod R) + 1) mod K_Pi (шаг 4 для d(2))
        const size_t source =
            (columnPermutation[position / rows] + subblockColumns * (position % rows) + 1) % interleavedLength;
        circularBuffer[interleavedLength + 2 * position + 1] = streamIndex(source, 2);
    }

    // Выбор бит: чтение по кругу с позиции k0, <NULL> пропускаются (5.1.4.1.2)
    const size_t bufferSize = circularBuffer.size();
    const size_t start = rows * (2 * ((bufferSize + 8 * rows - 1) / (8 * rows)) * redundancyVersion + 2);
    std::vector<size_t> map;
    map.reserve(outputLength);
    for (size_t offset = 0; map.size() < outputLength; ++offset)
    {
        const size_t position = circularBuffer[(start + offset) % bufferSize];
        if (position != nullPosition)
        {
            map.push_back(position);
        }
    }
    return map;
}

// Скремблирование PDSCH (TS 36.211 V16.2.0, 6.3.1): c_init = n_RNTI * 2^14 + q * 2^13 + floor(n_s / 2) * 2^9 + N_ID^cell
// Значение n_RNTI выбрано в проекте, q = 0 для одного кодового слова
constexpr uint32_t rnti = 1;
constexpr uint32_t codewordIndex = 0;
constexpr uint32_t scramblingInit =
    (rnti << 14) + (codewordIndex << 13) + ((slotNumber / 2) << 9) + cellId;

// Отображение QPSK (TS 36.211, 7.1.2, по памяти, страница не сверена): b(2i) задаёт I, b(2i+1) задаёт Q, 0 даёт +
Signal_t mapBitsToQpsk(std::span<const uint8_t> bits)
{
    Signal_t symbols;
    symbols.reserve(bits.size() / bitsPerSymbol);
    for (size_t index = 0; index + 1 < bits.size(); index += bitsPerSymbol)
    {
        symbols.push_back(qpskConstellation[size_t(bits[index] | (bits[index + 1] << 1))]);
    }
    return symbols;
}

// Мягкие значения бит (положительное — бит 0) по принятым символам и оценке канала.
// Дисперсия шума оценивается по расстоянию до ближайшей точки созвездия
std::vector<float> demapSoft(std::span<const Sample_t> received, std::span<const Sample_t> channel)
{
    constexpr double minNoiseVariance = 1e-12;

    double errorEnergy = 0.0;
    for (size_t index = 0; index < received.size(); ++index)
    {
        const Sample_t matched = received[index] * std::conj(channel[index]);
        const Sample_t nearest(std::copysign(qpskAmplitude, matched.real()), std::copysign(qpskAmplitude, matched.imag()));
        errorEnergy += std::norm(received[index] - channel[index] * nearest);
    }
    // Дисперсия одной квадратурной составляющей
    const double noiseVariance = std::max(errorEnergy / double(2 * received.size()), minNoiseVariance);

    std::vector<float> soft;
    soft.reserve(received.size() * bitsPerSymbol);
    for (size_t index = 0; index < received.size(); ++index)
    {
        const Sample_t matched = received[index] * std::conj(channel[index]);
        soft.push_back(float(2.0 * qpskAmplitude * matched.real() / noiseVariance));
        soft.push_back(float(2.0 * qpskAmplitude * matched.imag() / noiseVariance));
    }
    return soft;
}

class FftwPlan
{
public:
    FftwPlan() = default;
    explicit FftwPlan(fftw_plan plan) noexcept
        : m_plan(plan)
    {
    }
    ~FftwPlan()
    {
        if (m_plan != nullptr)
        {
            fftw_destroy_plan(m_plan);
        }
    }

    FftwPlan(const FftwPlan& other) = delete;
    FftwPlan(FftwPlan&& other) noexcept
        : m_plan(std::exchange(other.m_plan, nullptr))
    {
    }
    FftwPlan& operator=(const FftwPlan& other) = delete;
    FftwPlan& operator=(FftwPlan&& other) noexcept
    {
        std::swap(m_plan, other.m_plan);
        return *this;
    }

    bool IsValid() const { return m_plan != nullptr; }
    void Execute() const { fftw_execute(m_plan); }

private:
    fftw_plan m_plan = nullptr;
};

class OfdmModem
{
public:
    static std::unique_ptr<OfdmModem> Create()
    {
        auto modem = std::unique_ptr<OfdmModem>(new OfdmModem());

        auto* frequencyData = reinterpret_cast<fftw_complex*>(modem->m_frequencyBuffer.data());
        auto* timeData = reinterpret_cast<fftw_complex*>(modem->m_timeBuffer.data());
        modem->m_inversePlan =
            FftwPlan(fftw_plan_dft_1d(int(fftSize), frequencyData, timeData, FFTW_BACKWARD, FFTW_ESTIMATE));
        modem->m_forwardPlan =
            FftwPlan(fftw_plan_dft_1d(int(fftSize), timeData, frequencyData, FFTW_FORWARD, FFTW_ESTIMATE));

        if (!modem->m_inversePlan.IsValid() || !modem->m_forwardPlan.IsValid())
        {
            return nullptr;
        }
        return modem;
    }

    OfdmModem(const OfdmModem& other) = delete;
    OfdmModem(OfdmModem&& other) = delete;
    OfdmModem& operator=(const OfdmModem& other) = delete;
    OfdmModem& operator=(OfdmModem&& other) = delete;
    ~OfdmModem() = default;

    // Один OFDM-символ: циклический префикс и полезная часть
    Signal_t Modulate(std::span<const Sample_t> subcarriers, size_t cyclicPrefix)
    {
        std::fill(m_frequencyBuffer.begin(), m_frequencyBuffer.end(), Sample_t());
        for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
        {
            m_frequencyBuffer[frequencyBin(subcarrier)] = subcarriers[subcarrier];
        }

        m_inversePlan.Execute();
        // FFTW не нормирует обратное преобразование
        for (auto& sample : m_timeBuffer)
        {
            sample /= double(fftSize);
        }

        Signal_t symbol;
        symbol.reserve(cyclicPrefix + fftSize);
        symbol.insert(symbol.end(), m_timeBuffer.end() - std::ptrdiff_t(cyclicPrefix), m_timeBuffer.end());
        symbol.insert(symbol.end(), m_timeBuffer.begin(), m_timeBuffer.end());
        return symbol;
    }

    // usefulPart — fftSize отсчётов после циклического префикса
    std::array<Sample_t, subcarrierCount> Demodulate(std::span<const Sample_t> usefulPart)
    {
        std::copy(usefulPart.begin(), usefulPart.end(), m_timeBuffer.begin());
        m_forwardPlan.Execute();

        std::array<Sample_t, subcarrierCount> subcarriers;
        for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
        {
            subcarriers[subcarrier] = m_frequencyBuffer[frequencyBin(subcarrier)];
        }
        return subcarriers;
    }

private:
    OfdmModem()
        : m_frequencyBuffer(fftSize)
        , m_timeBuffer(fftSize)
    {
    }

    Signal_t m_frequencyBuffer;
    Signal_t m_timeBuffer;
    // Планы FFTW привязаны к адресам буферов: объявлены после них, чтобы разрушаться первыми
    FftwPlan m_inversePlan;
    FftwPlan m_forwardPlan;
};

// Размер кодового блока K из табл. 5.1.3-3 (TS 36.212 V15.2.1): при E = 2040 бит скорость кода около 1/3.
// Один блок без сегментации, так как B <= 6144 (5.1.2); B = нагрузка + CRC24A
constexpr size_t codeBlockSize = 672;
constexpr size_t payloadBytes = (codeBlockSize - crcLength) / bitsPerByte;
static_assert((codeBlockSize - crcLength) % bitsPerByte == 0);

class PhyLayer final : public IPhyLayer
{
public:
    PhyLayer(std::unique_ptr<OfdmModem> modem,
             std::unique_ptr<TurboCodec> turbo,
             std::shared_ptr<Logging::ILogger> logger,
             double sampleRateHz)
        : m_modem(std::move(modem))
        , m_turbo(std::move(turbo))
        , m_logger(std::move(logger))
        , m_sampleRateHz(sampleRateHz)
    {
        // Образец для поиска слота: символ с PSS, остальные поднесущие пусты
        ResourceGrid_t syncGrid{};
        mapSyncSignal(syncGrid);
        m_syncSymbol = m_modem->Modulate(syncGrid[syncSymbol], cyclicPrefixLength(syncSymbol));

        size_t dataElementCount = 0;
        for (size_t symbol = 0; symbol < symbolsPerSlot; ++symbol)
        {
            for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
            {
                dataElementCount += isDataElement(symbol, subcarrier) ? 1 : 0;
            }
        }

        // E = N_L * Q_m * G' при одном кодовом блоке, одном слое и QPSK (TS 36.212 V15.2.1, 5.1.4.1.2): все биты слота
        const size_t outputLength = dataElementCount * bitsPerSymbol;
        m_rateMatchingMap = buildRateMatchingMap(m_turbo->GetStreamLength(), outputLength);
        m_scrambling = generateGoldSequence(scramblingInit, outputLength);
    }

    size_t GetPayloadSize() const override { return payloadBytes; }

    std::optional<SyncPeak> FindSync(std::span<const Sample_t> signal) override
    {
        if (signal.size() < slotLength)
        {
            m_logger->Error(std::format("Signal of {} samples is shorter than a slot ({} samples)", signal.size(),
                                        slotLength));
            return std::nullopt;
        }

        // Сдвиг частоты смещает пик корреляции по времени, поэтому сначала перебираем гипотезы о сдвиге
        SyncPeak peak;
        double offsetHz = 0.0;
        for (double hypothesisHz = -searchRangeHz; hypothesisHz <= searchRangeHz; hypothesisHz += hypothesisStepHz)
        {
            const SyncPeak candidate = findCorrelationPeak(removeFrequencyOffset(signal, hypothesisHz));
            m_logger->Trace(std::format("Hypothesis {:.0f} Hz: slot at sample {}, correlation {:.3f}", hypothesisHz,
                                        candidate.position, candidate.correlation));
            if (candidate.correlation > peak.correlation)
            {
                peak = candidate;
                offsetHz = hypothesisHz;
            }
        }

        // У лучшей гипотезы остаточный сдвиг мал, и разность фаз половин оценивает его точно
        for (size_t pass = 0; pass < offsetEstimationPasses; ++pass)
        {
            offsetHz += estimateFrequencyOffset(removeFrequencyOffset(signal, offsetHz), peak.position);
            peak = findCorrelationPeak(removeFrequencyOffset(signal, offsetHz));
            m_logger->Trace(std::format("Pass {}: offset {:.0f} Hz, slot at sample {}, correlation {:.3f}", pass,
                                        offsetHz, peak.position, peak.correlation));
        }
        peak.frequencyOffsetHz = offsetHz;
        return peak;
    }

    std::optional<Signal_t> Modulate(std::span<const uint8_t> payload) override
    {
        if (payload.size() != payloadBytes)
        {
            m_logger->Error(std::format("Payload has to be {} bytes, got {}", payloadBytes, payload.size()));
            return std::nullopt;
        }

        const Bits_t block = attachCrc(bytesToBits(payload));
        const Bits_t streams = m_turbo->Encode(block);
        Bits_t codedBits(m_rateMatchingMap.size());
        for (size_t index = 0; index < codedBits.size(); ++index)
        {
            codedBits[index] = streams[m_rateMatchingMap[index]] ^ m_scrambling[index];
        }
        const Signal_t symbols = mapBitsToQpsk(codedBits);

        ResourceGrid_t grid{};
        mapSyncSignal(grid);
        mapReferenceSignals(grid);
        size_t nextSymbol = 0;
        for (size_t symbol = 0; symbol < symbolsPerSlot; ++symbol)
        {
            for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
            {
                if (isDataElement(symbol, subcarrier))
                {
                    grid[symbol][subcarrier] = symbols[nextSymbol++];
                }
            }
        }

        Signal_t slot;
        slot.reserve(slotLength);
        for (size_t symbol = 0; symbol < symbolsPerSlot; ++symbol)
        {
            const Signal_t samples = m_modem->Modulate(grid[symbol], cyclicPrefixLength(symbol));
            slot.insert(slot.end(), samples.begin(), samples.end());
        }
        m_logger->Trace(std::format("{} payload bytes -> {} block bits -> {} coded bits -> {} samples", payload.size(),
                                    block.size(), codedBits.size(), slot.size()));
        return slot;
    }

    std::optional<ReceivedFrame> Demodulate(std::span<const Sample_t> frame, double frequencyOffsetHz) override
    {
        if (frame.size() < slotLength)
        {
            m_logger->Error(std::format("Received signal of {} samples is shorter than a slot ({} samples)",
                                        frame.size(), slotLength));
            return std::nullopt;
        }

        const Signal_t corrected = removeFrequencyOffset(frame.first(slotLength), frequencyOffsetHz);
        ResourceGrid_t received;
        for (size_t symbol = 0; symbol < symbolsPerSlot; ++symbol)
        {
            const auto usefulPart =
                std::span(corrected).subspan(symbolOffset(symbol) + cyclicPrefixLength(symbol), fftSize);
            received[symbol] = m_modem->Demodulate(usefulPart);
        }

        const ResourceGrid_t channel = estimateChannel(received);
        Signal_t dataSymbols;
        Signal_t dataChannel;
        for (size_t symbol = 0; symbol < symbolsPerSlot; ++symbol)
        {
            for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
            {
                if (isDataElement(symbol, subcarrier))
                {
                    dataSymbols.push_back(received[symbol][subcarrier]);
                    dataChannel.push_back(channel[symbol][subcarrier]);
                }
            }
        }

        std::vector<float> soft = demapSoft(dataSymbols, dataChannel);
        Bytes_t codedBits(soft.size());
        for (size_t index = 0; index < soft.size(); ++index)
        {
            codedBits[index] = soft[index] < 0.0F ? 1 : 0;
            if (m_scrambling[index] != 0)
            {
                soft[index] = -soft[index];
            }
        }

        // Повторно переданные биты (E больше числа бит буфера) складываются
        std::vector<float> streams(turboStreamCount * m_turbo->GetStreamLength(), 0.0F);
        for (size_t index = 0; index < soft.size(); ++index)
        {
            streams[m_rateMatchingMap[index]] += soft[index];
        }

        std::optional<Bytes_t> payload;
        const auto block = m_turbo->Decode(streams);
        if (block.has_value() && calculateCrc(*block) == 0)
        {
            payload = bitsToBytes(std::span(*block).first(codeBlockSize - crcLength));
        }
        m_logger->Trace(std::format("{} coded bits -> CRC ok: {}", codedBits.size(), payload.has_value()));
        return ReceivedFrame{
            .codedBits = std::move(codedBits),
            .payload = std::move(payload),
        };
    }

private:
    Signal_t removeFrequencyOffset(std::span<const Sample_t> signal, double offsetHz) const
    {
        Signal_t corrected(signal.size());
        for (size_t index = 0; index < signal.size(); ++index)
        {
            const double phase = -2.0 * std::numbers::pi * offsetHz * double(index) / m_sampleRateHz;
            corrected[index] = signal[index] * std::polar(1.0, phase);
        }
        return corrected;
    }

    // Нормированная корреляция символа с PSS в каждой позиции слота, берётся лучшая
    SyncPeak findCorrelationPeak(std::span<const Sample_t> signal) const
    {
        const size_t syncSize = m_syncSymbol.size();
        double syncEnergy = 0.0;
        for (const auto& sample : m_syncSymbol)
        {
            syncEnergy += std::norm(sample);
        }

        SyncPeak peak;
        // Слот целиком должен помещаться в сигнал
        for (size_t position = 0; position + slotLength <= signal.size(); ++position)
        {
            const size_t syncStart = position + symbolOffset(syncSymbol);
            Sample_t product;
            double energy = 0.0;
            for (size_t index = 0; index < syncSize; ++index)
            {
                product += std::conj(m_syncSymbol[index]) * signal[syncStart + index];
                energy += std::norm(signal[syncStart + index]);
            }
            // По неравенству Коши — Буняковского значение не больше 1
            const double correlation = energy > 0.0 ? std::sqrt(std::norm(product) / (syncEnergy * energy)) : 0.0;
            if (correlation > peak.correlation)
            {
                peak.position = position;
                peak.correlation = correlation;
            }
        }
        return peak;
    }

    // Фаза между корреляциями двух половин PSS растёт на 2*pi*f*T, T — половина длительности
    double estimateFrequencyOffset(std::span<const Sample_t> signal, size_t slotStart) const
    {
        const size_t halfSize = m_syncSymbol.size() / 2;
        const size_t syncStart = slotStart + symbolOffset(syncSymbol);
        Sample_t firstHalf;
        Sample_t secondHalf;
        for (size_t index = 0; index < halfSize; ++index)
        {
            firstHalf += std::conj(m_syncSymbol[index]) * signal[syncStart + index];
            secondHalf += std::conj(m_syncSymbol[halfSize + index]) * signal[syncStart + halfSize + index];
        }
        const double phase = std::arg(secondHalf * std::conj(firstHalf));
        return phase * m_sampleRateHz / (2.0 * std::numbers::pi * double(halfSize));
    }

    std::unique_ptr<OfdmModem> m_modem;
    std::unique_ptr<TurboCodec> m_turbo;
    std::shared_ptr<Logging::ILogger> m_logger;
    double m_sampleRateHz;
    Signal_t m_syncSymbol;
    std::vector<size_t> m_rateMatchingMap;
    std::vector<uint8_t> m_scrambling;
};

} // namespace

std::unique_ptr<IPhyLayer> createPhyLayer(const PhyConfig& config, std::shared_ptr<Logging::ILogger> logger)
{
    if (logger == nullptr)
    {
        return nullptr;
    }

    auto modem = OfdmModem::Create();
    if (modem == nullptr)
    {
        logger->Error(std::format("Cannot create PHY layer: FFTW planning of size {} failed", fftSize));
        return nullptr;
    }

    auto turbo = TurboCodec::Create(codeBlockSize);
    if (turbo == nullptr)
    {
        logger->Error(std::format("Cannot create PHY layer: turbo codec for block size {} failed", codeBlockSize));
        return nullptr;
    }

    return std::make_unique<PhyLayer>(std::move(modem), std::move(turbo), std::move(logger), config.sampleRateHz);
}

} // namespace Radiocom::SignalManipulation
