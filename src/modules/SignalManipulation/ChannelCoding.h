#pragma once

#include "Ofdm.h"
#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <Module/Decoder/RSC/BCJR/Seq_generic/Decoder_RSC_BCJR_seq_generic_std.hpp>
#include <Module/Decoder/Turbo/Decoder_turbo_std.hpp>
#include <Module/Encoder/RSC/Encoder_RSC_generic_sys.hpp>
#include <Module/Encoder/Turbo/Encoder_turbo.hpp>
#include <Module/Interleaver/Interleaver.hpp>
#include <Tools/Interleaver/LTE/Interleaver_core_LTE.hpp>


namespace Radiocom::SignalManipulation
{

using Bits_t = std::vector<uint8_t>;

inline constexpr size_t bitsPerByte = 8;

// Проектное соглашение: байты нагрузки раскладываются в биты старшим битом вперёд (в стандарте для этого нет страницы)
Bits_t bytesToBits(std::span<const uint8_t> bytes);
Bytes_t bitsToBytes(std::span<const uint8_t> bits);

// CRC24A транспортного блока и CRC24B кодового блока (TS 36.212 V15.2.1, 5.1.1, 5.1.2)
enum class CrcType
{
    TransportBlock,
    CodeBlock,
};

inline constexpr size_t crcLength = 24;

// Остаток от деления bits * D^24 на g(D); для блока с присоединённой CRC остаток равен нулю (5.1.1)
uint32_t calculateCrc(std::span<const uint8_t> bits, CrcType type = CrcType::TransportBlock);
// Биты чётности идут после информационных, p_0 — старший бит остатка, коэффициент при D^23 (5.1.1)
Bits_t attachCrc(std::span<const uint8_t> bits, CrcType type = CrcType::TransportBlock);

// Модуляция Q_m и размер транспортного блока по индексу MCS для PDSCH на 15 RB
// (TS 36.213 V17.1.0, табл. 7.1.7.1-1 и столбец N_PRB = 15 табл. 7.1.7.2.1-1)
struct ModulationAndCoding
{
    size_t modulationOrder = 0;
    size_t transportBlockSize = 0;
};

inline constexpr size_t maxMcsIndex = 28;
std::optional<ModulationAndCoding> findModulationAndCoding(size_t mcsIndex);

// Размер блока K есть в табл. 5.1.3-3 (TS 36.212 V15.2.1): 40..512 с шагом 8, 528..1024 с шагом 16,
// 1056..2048 с шагом 32, 2112..6144 с шагом 64
bool isTurboBlockSize(size_t blockSize);

// Сегментация на кодовые блоки (TS 36.212 V15.2.1, 5.1.2): C блоков размера K
struct Segmentation
{
    size_t blockCount = 0;
    size_t blockSize = 0;
};

// bitCount — B, транспортный блок с CRC24A. В проекте поддержаны только блоки одной длины без заполняющих бит (F = 0,
// C_- = 0); для остальных B возвращает std::nullopt
std::optional<Segmentation> segmentTransportBlock(size_t bitCount);

// При C > 1 у каждого кодового блока последние 24 бита — CRC24B (5.1.2)
constexpr size_t codeBlockPayloadSize(const Segmentation& segmentation)
{
    return segmentation.blockCount == 1 ? segmentation.blockSize : segmentation.blockSize - crcLength;
}

// Длины E_r выхода согласования скорости кодовых блоков r = 0..C-1 при G бит на транспортный блок и одном слое:
// G' = G / Q_m, gamma = G' mod C; E_r = Q_m * floor(G' / C) при r <= C - gamma - 1, иначе Q_m * ceil(G' / C)
// (TS 36.212 V15.2.1, 5.1.4.1.2)
std::vector<size_t> rateMatchingOutputLengths(size_t availableBits, size_t modulationOrder, size_t blockCount);

inline constexpr size_t turboStreamCount = 3;
// Каждый из потоков d(0), d(1), d(2) длиннее блока на 4 бита завершения решётки (TS 36.212 V15.2.1, 5.1.3.2.2)
inline constexpr size_t turboTailLength = 4;

// Турбокод (TS 36.212 V15.2.1, 5.1.3.2) на модулях AFF3CT
class TurboCodec
{
public:
    // Размер блока должен быть в табл. 5.1.3-3 (TS 36.212), иначе фабрика возвращает nullptr
    static std::unique_ptr<TurboCodec> Create(size_t blockSize);

    TurboCodec(const TurboCodec& other) = delete;
    TurboCodec(TurboCodec&& other) = delete;
    TurboCodec& operator=(const TurboCodec& other) = delete;
    TurboCodec& operator=(TurboCodec&& other) = delete;
    ~TurboCodec() = default;

    size_t GetStreamLength() const { return size_t(m_blockSize) + turboTailLength; }

    // Потоки d(0), d(1), d(2) подряд: [d(0) | d(1) | d(2)]
    Bits_t Encode(std::span<const uint8_t> block);
    // Мягкие значения бит в том же порядке, что результат Encode; положительное значение — бит 0
    std::optional<Bits_t> Decode(std::span<const float> streams);

private:
    explicit TurboCodec(int blockSize);

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

// Согласование скорости турбокода (TS 36.212 V15.2.1, 5.1.4.1): для каждого из outputLength выходных бит e_k
// возвращает его позицию в [d(0) | d(1) | d(2)].
// Размер кольцевого буфера N_cb принят равным K_w: ограничения по буферу приёмника (N_IR, 5.1.4.1.2) в проекте нет
std::vector<size_t> buildRateMatchingMap(size_t streamLength, size_t outputLength);

// Скремблирование PDSCH (TS 36.211 V16.2.0, 6.3.1): c_init = n_RNTI * 2^14 + q * 2^13 + floor(n_s / 2) * 2^9 + N_ID^cell,
// floor(n_s / 2) — номер субкадра. Значение n_RNTI выбрано в проекте, q = 0 для одного кодового слова
inline constexpr uint32_t rnti = 1;
inline constexpr uint32_t codewordIndex = 0;

constexpr uint32_t scramblingInit(size_t subframe)
{
    return (rnti << 14) + (codewordIndex << 13) + (uint32_t(subframe) << 9) + uint32_t(cellId);
}

// QPSK, 16QAM и 64QAM (TS 36.211 V16.2.0, табл. 7.1.2-1, 7.1.3-1, 7.1.4-1); modulationOrder = Q_m = 2, 4 или 6
bool isSupportedModulation(size_t modulationOrder);

// Группы по Q_m бит b(i)..b(i+Q_m-1) в символы: биты с чётным номером в группе задают I, с нечётным — Q
Signal_t mapBits(std::span<const uint8_t> bits, size_t modulationOrder);
// Мягкие значения бит (положительное — бит 0) по принятым символам и оценке канала, max-log по каждой оси.
// Дисперсия шума оценивается по расстоянию до ближайшей точки созвездия
std::vector<float> demapSoft(std::span<const Sample_t> received,
                             std::span<const Sample_t> channel,
                             size_t modulationOrder);

} // namespace Radiocom::SignalManipulation
