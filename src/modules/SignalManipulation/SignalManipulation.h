#pragma once

#include "Logger.h"
#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>


namespace Radiocom::SignalManipulation
{

struct ReceivedSubframe
{
    // Номер субкадра в кадре
    size_t subframe = 0;
    // Жёсткие решения для бит после скремблирования, до декодирования; один бит в элементе
    Bytes_t codedBits;
    // Есть, только если CRC принятого транспортного блока верна
    std::optional<Bytes_t> payload;
};

struct SyncPeak
{
    // Индекс отсчёта, с которого начинается кадр; кадр целиком помещается в сигнал
    size_t position = 0;
    // Нормированная корреляция PSS, от 0 до 1
    double correlation = 0.0;
    // Оценка сдвига частоты принятого сигнала относительно переданного
    double frequencyOffsetHz = 0.0;
};

class IPhyLayer
{
public:
    virtual ~IPhyLayer() = default;

    // Длина транспортного блока одного субкадра в байтах, задана индексом MCS
    virtual size_t GetPayloadSize() const = 0;
    // Длина кадра 10 мс в отсчётах
    virtual size_t GetFrameLength() const = 0;
    // Номера субкадров с данными, по одному транспортному блоку в каждом
    virtual std::span<const size_t> GetDataSubframes() const = 0;

    // Ищет кадр по PSS и SSS и оценивает сдвиг частоты в пределах ±15 кГц
    virtual std::optional<SyncPeak> FindSync(std::span<const Sample_t> signal) = 0;

    // Кадр TDD (тип 2, конфигурация UL/DL 1): в каждом субкадре из GetDataSubframes() транспортный блок
    // CRC24A → турбокод → согласование скорости → скремблирование → модуляция; payloads — по одному на субкадр с данными,
    // каждый размера GetPayloadSize()
    virtual std::optional<Signal_t> Modulate(std::span<const Bytes_t> payloads) = 0;
    // frame должен начинаться с первого отсчёта кадра; сдвиг частоты из FindSync убирается здесь.
    // Результат — по одному элементу на субкадр с данными
    virtual std::optional<std::vector<ReceivedSubframe>> Demodulate(std::span<const Sample_t> frame,
                                                                    double frequencyOffsetHz) = 0;
};

struct PhyConfig
{
    double sampleRateHz = 3'840'000.0;
    // I_MCS (TS 36.213 V17.1.0, табл. 7.1.7.1-1), одинаковый у передатчика и приёмника
    size_t mcsIndex = 0;
};

std::unique_ptr<IPhyLayer> createPhyLayer(const PhyConfig& config, std::shared_ptr<Logging::ILogger> logger);

} // namespace Radiocom::SignalManipulation
