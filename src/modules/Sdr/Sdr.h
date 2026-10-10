#pragma once

#include "Logger.h"
#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>


struct iio_context;
struct iio_device;
struct iio_channel;
struct iio_buffer;

namespace Radiocom::Sdr
{

struct PlutoConfig
{
    std::string uri;
    int64_t centerFrequencyHz = 2'400'000'000;
    int64_t sampleRateHz = 3'840'000;
    int64_t rfBandwidthHz = 3'000'000;
    // Ослабление передатчика в дБ (положительное число), меньше 40 дБ не принимается
    double txAttenuationDb = 89.75;
    // Ручное усиление приёмника, AGC выключен
    double rxGainDb = 0.0;
};

// Pluto SDR через libiio: первый канал TX и первый канал RX
class PlutoDevice
{
public:
    static std::unique_ptr<PlutoDevice> Create(const PlutoConfig& config, std::shared_ptr<Logging::ILogger> logger);

    PlutoDevice(const PlutoDevice& other) = delete;
    PlutoDevice& operator=(const PlutoDevice& other) = delete;
    ~PlutoDevice();

    // Циклическая передача: буфер повторяется, пока не вызван StopTransmit.
    // Отсчёты в диапазоне [-1, 1] по I и Q
    bool StartTransmit(std::span<const Sample_t> signal);
    void StopTransmit();

    // Отсчёты в диапазоне [-1, 1] по I и Q
    std::optional<Signal_t> Receive(size_t sampleCount);

private:
    explicit PlutoDevice(std::shared_ptr<Logging::ILogger> logger);

    using Context_t = std::unique_ptr<iio_context, void (*)(iio_context*)>;
    using Buffer_t = std::unique_ptr<iio_buffer, void (*)(iio_buffer*)>;

    std::shared_ptr<Logging::ILogger> m_logger;
    // Контекст объявлен первым, чтобы разрушаться последним
    Context_t m_context;
    iio_device* m_txDevice = nullptr;
    iio_device* m_rxDevice = nullptr;
    iio_channel* m_txGainChannel = nullptr;
    iio_channel* m_txI = nullptr;
    iio_channel* m_txQ = nullptr;
    iio_channel* m_rxI = nullptr;
    iio_channel* m_rxQ = nullptr;
    Buffer_t m_txBuffer;
};

} // namespace Radiocom::Sdr
