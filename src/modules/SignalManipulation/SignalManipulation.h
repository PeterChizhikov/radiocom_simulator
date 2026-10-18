#pragma once

#include "Logger.h"
#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>


namespace Radiocom::SignalManipulation
{

struct ReceivedFrame
{
    // Жёсткие решения для бит после скремблирования, до декодирования; один бит в элементе
    Bytes_t codedBits;
    // Есть, только если CRC принятого блока верна
    std::optional<Bytes_t> payload;
};

struct SyncPeak
{
    // Индекс отсчёта, с которого начинается слот
    size_t position = 0;
    // Нормированная корреляция, от 0 до 1
    double correlation = 0.0;
    // Оценка сдвига частоты принятого сигнала относительно переданного
    double frequencyOffsetHz = 0.0;
};

class IPhyLayer
{
public:
    virtual ~IPhyLayer() = default;

    // Длина нагрузки в слоте в байтах, фиксирована размером кодового блока
    virtual size_t GetPayloadSize() const = 0;

    // Ищет слот по PSS кросс-корреляцией и оценивает сдвиг частоты в пределах ±15 кГц
    virtual std::optional<SyncPeak> FindSync(std::span<const Sample_t> signal) = 0;

    // CRC24A → турбокод → согласование скорости → скремблирование → QPSK → слот LTE
    // (7 OFDM-символов, PSS в последнем, пилоты CRS и данные в остальных); payload.size() == GetPayloadSize()
    virtual std::optional<Signal_t> Modulate(std::span<const uint8_t> payload) = 0;
    // frame должен начинаться с первого отсчёта слота; сдвиг частоты из FindSync убирается здесь
    virtual std::optional<ReceivedFrame> Demodulate(std::span<const Sample_t> frame, double frequencyOffsetHz) = 0;
};

struct PhyConfig
{
    double sampleRateHz = 3'840'000.0;
};

std::unique_ptr<IPhyLayer> createPhyLayer(const PhyConfig& config, std::shared_ptr<Logging::ILogger> logger);

} // namespace Radiocom::SignalManipulation
