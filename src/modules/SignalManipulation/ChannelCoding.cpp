#include "ChannelCoding.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>


namespace Radiocom::SignalManipulation
{
namespace
{

// g_CRC24A(D) = D^24 + D^23 + D^18 + D^17 + D^14 + D^11 + D^10 + D^7 + D^6 + D^5 + D^4 + D^3 + D + 1,
// g_CRC24B(D) = D^24 + D^23 + D^6 + D^5 + D + 1, здесь без члена D^24 (TS 36.212 V15.2.1, 5.1.1)
constexpr uint32_t crc24APolynomial = 0x864CFB;
constexpr uint32_t crc24BPolynomial = 0x800063;
constexpr uint32_t crcMask = (1U << crcLength) - 1;

// Два составных кодера с передаточной функцией [1, g1(D)/g0(D)], g0(D) = 1 + D^2 + D^3, g1(D) = 1 + D + D^3,
// восемь состояний (TS 36.212 V15.2.1, 5.1.3.2.1), перемежитель QPP (5.1.3.2.3)
constexpr std::array<int, 2> turboPolynomials = {013, 015};
constexpr int turboMemory = 3;
// Число итераций декодера выбрано в проекте
constexpr int turboIterations = 6;

// C_subblock (TS 36.212 V15.2.1, 5.1.4.1.1, шаг 1)
constexpr size_t subblockColumns = 32;
// Перестановка столбцов, табл. 5.1.4-1
constexpr std::array<size_t, subblockColumns> columnPermutation = {0, 16, 8,  24, 4, 20, 12, 28, 2, 18, 10,
                                                                    26, 6, 22, 14, 30, 1,  17, 9,  25, 5, 21,
                                                                    13, 29, 3,  19, 11, 27, 7,  23, 15, 31};
// rv_idx первой передачи (5.1.4.1.2)
constexpr size_t redundancyVersion = 0;
constexpr size_t nullPosition = SIZE_MAX;

// Q_m и I_TBS для I_MCS = 0..28 (TS 36.213 V17.1.0, табл. 7.1.7.1-1)
constexpr std::array<size_t, maxMcsIndex + 1> mcsModulationOrders = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 4, 4, 4, 4,
                                                                     4, 4, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6};
constexpr std::array<size_t, maxMcsIndex + 1> mcsTbsIndices = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
                                                               9,  10, 11, 12, 13, 14, 15, 15, 16, 17,
                                                               18, 19, 20, 21, 22, 23, 24, 25, 26};
// Размер транспортного блока для I_TBS = 0..26 при N_PRB = 15 (TS 36.213 V17.1.0, табл. 7.1.7.2.1-1)
constexpr std::array<size_t, 27> transportBlockSizes = {392,  520,  648,  872,  1064, 1320, 1544, 1800, 2088,
                                                        2344, 2664, 2984, 3368, 3880, 4264, 4584, 4968, 5352,
                                                        5992, 6456, 6968, 7480, 7992, 8504, 9144, 9528, 11064};

// Z и длина CRC кодового блока L (TS 36.212 V15.2.1, 5.1.2)
constexpr size_t maxCodeBlockSize = 6144;
constexpr size_t codeBlockCrcLength = 24;

// Уровни одной оси созвездия, индекс — биты этой оси: бит j индекса — j-й бит оси в группе (для I это b(i + 2j),
// для Q — b(i + 2j + 1)). Таблицы 7.1.2-1, 7.1.3-1, 7.1.4-1 (TS 36.211 V16.2.0) записываются как код Грея по оси:
// знак задаёт первый бит (0 — плюс), модуль — остальные: 1 бит: 1 или 3; 2 бита: 00 — 3, 01 — 1, 10 — 5, 11 — 7.
// В общем виде t = 1, затем t = 2^(n - j) - (1 - 2 b_j) * t для j = n-1..1, уровень (1 - 2 b_0) * t. Нормировка
// 1/sqrt(2), 1/sqrt(10), 1/sqrt(42) — средняя мощность символа 1, то есть 1/sqrt(2 (M - 1) / 3) при M = 2^Q_m
std::vector<double> axisLevels(size_t modulationOrder)
{
    const size_t axisBits = modulationOrder / 2;
    const double pointCount = double(size_t(1) << modulationOrder);
    const double scale = 1.0 / std::sqrt(2.0 * (pointCount - 1.0) / 3.0);

    std::vector<double> levels(size_t(1) << axisBits);
    for (size_t pattern = 0; pattern < levels.size(); ++pattern)
    {
        const auto sign = [&](size_t bit) { return ((pattern >> bit) & 1U) != 0 ? -1.0 : 1.0; };
        double magnitude = 1.0;
        for (size_t bit = axisBits - 1; bit >= 1; --bit)
        {
            magnitude = double(size_t(1) << (axisBits - bit)) - (sign(bit) * magnitude);
        }
        levels[pattern] = scale * sign(0) * magnitude;
    }
    return levels;
}

// Биты одной оси группы из Q_m бит: offset 0 — ось I, 1 — ось Q
size_t axisPattern(std::span<const uint8_t> group, size_t offset)
{
    size_t pattern = 0;
    for (size_t bit = 0; (2 * bit) + offset < group.size(); ++bit)
    {
        pattern |= size_t(group[(2 * bit) + offset] & 1U) << bit;
    }
    return pattern;
}

double nearestLevel(std::span<const double> levels, double value)
{
    return *std::ranges::min_element(levels, {}, [&](double level) { return std::abs(value - level); });
}

// AFF3CT выдаёт слово [x | хвост x | z | хвост z | хвост x' | z' | хвост z'], где x — информационные биты, z и z' —
// чётность первого и второго кодера, x' — информационные биты второго кодера; хвосты по три бита.
// Здесь для каждой позиции [d(0) | d(1) | d(2)] найден индекс в слове AFF3CT по формулам 5.1.3.2.2
std::vector<size_t> buildTurboLayout(size_t blockSize)
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
    layout.insert(layout.end(), {tailSystematic, tailParity + 1, tailSystematicSecond, tailParitySecond + 1});
    for (size_t index = 0; index < blockSize; ++index)
    {
        layout.push_back(parity + index);
    }
    layout.insert(layout.end(), {tailParity, tailSystematic + 2, tailParitySecond, tailSystematicSecond + 2});
    for (size_t index = 0; index < blockSize; ++index)
    {
        layout.push_back(paritySecond + index);
    }
    layout.insert(layout.end(), {tailSystematic + 1, tailParity + 2, tailSystematicSecond + 1, tailParitySecond + 2});
    return layout;
}

} // namespace

std::optional<ModulationAndCoding> findModulationAndCoding(size_t mcsIndex)
{
    if (mcsIndex > maxMcsIndex)
    {
        return std::nullopt;
    }
    return ModulationAndCoding{
        .modulationOrder = mcsModulationOrders[mcsIndex],
        .transportBlockSize = transportBlockSizes[mcsTbsIndices[mcsIndex]],
    };
}

bool isTurboBlockSize(size_t blockSize)
{
    struct Range
    {
        size_t first;
        size_t last;
        size_t step;
    };
    constexpr std::array<Range, 4> ranges = {{{40, 512, 8}, {528, 1024, 16}, {1056, 2048, 32}, {2112, 6144, 64}}};
    return std::ranges::any_of(ranges, [&](const Range& range)
                               { return blockSize >= range.first && blockSize <= range.last &&
                                        (blockSize - range.first) % range.step == 0; });
}

std::optional<Segmentation> segmentTransportBlock(size_t bitCount)
{
    // C = 1 без CRC блока при B <= Z, иначе C = ceil(B / (Z - L)) и B' = B + C * L (5.1.2)
    const size_t blockCount =
        bitCount <= maxCodeBlockSize ? 1 : (bitCount + maxCodeBlockSize - codeBlockCrcLength - 1) /
                                               (maxCodeBlockSize - codeBlockCrcLength);
    const size_t totalBits = blockCount == 1 ? bitCount : bitCount + blockCount * codeBlockCrcLength;

    // K+ — наименьший K из табл. 5.1.3-3, при котором C * K >= B'; все блоки размера K+ только при C * K+ == B',
    // тогда F = 0 и C_- = 0
    size_t blockSize = (totalBits + blockCount - 1) / blockCount;
    while (blockSize <= maxCodeBlockSize && !isTurboBlockSize(blockSize))
    {
        ++blockSize;
    }
    if (blockSize > maxCodeBlockSize || blockCount * blockSize != totalBits)
    {
        return std::nullopt;
    }
    return Segmentation{.blockCount = blockCount, .blockSize = blockSize};
}

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

std::vector<size_t> rateMatchingOutputLengths(size_t availableBits, size_t modulationOrder, size_t blockCount)
{
    const size_t symbolCount = availableBits / modulationOrder;
    const size_t remainder = symbolCount % blockCount;
    std::vector<size_t> lengths;
    for (size_t block = 0; block < blockCount; ++block)
    {
        const size_t symbols = (symbolCount / blockCount) + (block + remainder >= blockCount ? 1 : 0);
        lengths.push_back(modulationOrder * symbols);
    }
    return lengths;
}

uint32_t calculateCrc(std::span<const uint8_t> bits, CrcType type)
{
    const uint32_t polynomial = type == CrcType::TransportBlock ? crc24APolynomial : crc24BPolynomial;
    uint32_t remainder = 0;
    for (const uint8_t bit : bits)
    {
        const uint32_t feedback = ((remainder >> (crcLength - 1)) & 1U) ^ bit;
        remainder = (remainder << 1) & crcMask;
        if (feedback != 0)
        {
            remainder ^= polynomial;
        }
    }
    return remainder;
}

Bits_t attachCrc(std::span<const uint8_t> bits, CrcType type)
{
    const uint32_t remainder = calculateCrc(bits, type);
    Bits_t block(bits.begin(), bits.end());
    for (size_t index = 0; index < crcLength; ++index)
    {
        block.push_back((remainder >> (crcLength - 1 - index)) & 1U);
    }
    return block;
}

std::unique_ptr<TurboCodec> TurboCodec::Create(size_t blockSize)
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

TurboCodec::TurboCodec(int blockSize)
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
    , m_layout(buildTurboLayout(size_t(blockSize)))
{
}

Bits_t TurboCodec::Encode(std::span<const uint8_t> block)
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

std::optional<Bits_t> TurboCodec::Decode(std::span<const float> streams)
{
    for (size_t position = 0; position < streams.size(); ++position)
    {
        m_llr[m_layout[position]] = streams[position];
    }
    m_decoder.decode_siho(m_llr, m_decoded);
    return Bits_t(m_decoded.begin(), m_decoded.end());
}

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

bool isSupportedModulation(size_t modulationOrder)
{
    return modulationOrder == 2 || modulationOrder == 4 || modulationOrder == 6;
}

Signal_t mapBits(std::span<const uint8_t> bits, size_t modulationOrder)
{
    const std::vector<double> levels = axisLevels(modulationOrder);
    Signal_t symbols;
    symbols.reserve(bits.size() / modulationOrder);
    for (size_t index = 0; index + modulationOrder <= bits.size(); index += modulationOrder)
    {
        const auto group = bits.subspan(index, modulationOrder);
        symbols.emplace_back(levels[axisPattern(group, 0)], levels[axisPattern(group, 1)]);
    }
    return symbols;
}

std::vector<float> demapSoft(std::span<const Sample_t> received,
                             std::span<const Sample_t> channel,
                             size_t modulationOrder)
{
    constexpr double minNoiseVariance = 1e-12;
    const std::vector<double> levels = axisLevels(modulationOrder);
    const size_t axisBits = modulationOrder / 2;

    // Выровненный символ Y / H и мощность канала |H|^2: в выровненной области шум оси имеет дисперсию sigma^2 / |H|^2
    std::vector<Sample_t> equalized(received.size());
    std::vector<double> gains(received.size());
    double errorEnergy = 0.0;
    for (size_t index = 0; index < received.size(); ++index)
    {
        gains[index] = std::norm(channel[index]);
        equalized[index] = gains[index] > 0.0 ? received[index] / channel[index] : Sample_t();
        const Sample_t nearest(nearestLevel(levels, equalized[index].real()),
                               nearestLevel(levels, equalized[index].imag()));
        errorEnergy += std::norm(received[index] - channel[index] * nearest);
    }
    // Дисперсия одной квадратурной составляющей
    const double noiseVariance = std::max(errorEnergy / double(2 * received.size()), minNoiseVariance);

    // LLR = |H|^2 * (min_{s: b = 1} (z - s)^2 - min_{s: b = 0} (z - s)^2) / (2 sigma^2); для QPSK это 2 A Re(Y H*) / sigma^2
    std::vector<float> soft(received.size() * modulationOrder);
    const auto demapAxis = [&](double value, double gain, size_t offset, std::span<float> output)
    {
        for (size_t bit = 0; bit < axisBits; ++bit)
        {
            std::array<double, 2> distances = {std::numeric_limits<double>::infinity(),
                                               std::numeric_limits<double>::infinity()};
            for (size_t pattern = 0; pattern < levels.size(); ++pattern)
            {
                const double distance = (value - levels[pattern]) * (value - levels[pattern]);
                double& best = distances[(pattern >> bit) & 1U];
                best = std::min(best, distance);
            }
            output[(2 * bit) + offset] = float(gain * (distances[1] - distances[0]) / (2.0 * noiseVariance));
        }
    };
    for (size_t index = 0; index < received.size(); ++index)
    {
        const auto output = std::span(soft).subspan(index * modulationOrder, modulationOrder);
        demapAxis(equalized[index].real(), gains[index], 0, output);
        demapAxis(equalized[index].imag(), gains[index], 1, output);
    }
    return soft;
}

} // namespace Radiocom::SignalManipulation
