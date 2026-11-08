#include "ChannelCoding.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>


namespace Radiocom::SignalManipulation
{
namespace
{

// g(D) = D^24 + D^23 + D^18 + D^17 + D^14 + D^11 + D^10 + D^7 + D^6 + D^5 + D^4 + D^3 + D + 1, здесь без члена D^24
// (TS 36.212 V15.2.1, 5.1.1)
constexpr uint32_t crcPolynomial = 0x864CFB;
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

constexpr double qpskAmplitude = 0.707;

constexpr std::array<Sample_t, 4> qpskConstellation = {
    Sample_t(qpskAmplitude, qpskAmplitude),
    Sample_t(-qpskAmplitude, qpskAmplitude),
    Sample_t(qpskAmplitude, -qpskAmplitude),
    Sample_t(-qpskAmplitude, -qpskAmplitude),
};

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

} // namespace Radiocom::SignalManipulation
