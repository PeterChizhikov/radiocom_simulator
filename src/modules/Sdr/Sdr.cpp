#include "Sdr.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <utility>

#include <iio.h>


namespace Radiocom::Sdr
{
namespace
{

// Предохранитель приёмного тракта: меньшее ослабление передатчика не допускается
constexpr double minTxAttenuationDb = 40.0;
constexpr double maxTxAttenuationDb = 89.75;

// ЦАП 12-битный, отсчёт выровнен по старшим битам int16
constexpr double txFullScale = 2047.0 * 16.0;
// АЦП 12-битный, отсчёт выровнен по младшим битам int16
constexpr double rxFullScale = 2048.0;

std::string describeError(int error)
{
    std::array<char, 256> text = {};
    iio_strerror(error, text.data(), text.size());
    return text.data();
}

bool writeAttribute(const iio_channel* channel, const char* name, const std::string& value, Logging::ILogger& logger)
{
    const ssize_t result = iio_channel_attr_write(channel, name, value.c_str());
    if (result < 0)
    {
        logger.Error(std::format("Cannot write {} = {} to channel {}: {}", name, value, iio_channel_get_id(channel),
                                 describeError(int(-result))));
        return false;
    }
    logger.Trace(std::format("Channel {}: {} = {}", iio_channel_get_id(channel), name, value));
    return true;
}

int16_t toDacSample(double value)
{
    return int16_t(std::lround(std::clamp(value, -1.0, 1.0) * txFullScale));
}

} // namespace

PlutoDevice::PlutoDevice(std::shared_ptr<Logging::ILogger> logger)
    : m_logger(std::move(logger))
    , m_context(nullptr, iio_context_destroy)
    , m_txBuffer(nullptr, iio_buffer_destroy)
{
}

PlutoDevice::~PlutoDevice()
{
    StopTransmit();
    if (m_txGainChannel != nullptr)
    {
        writeAttribute(m_txGainChannel, "hardwaregain", std::to_string(-maxTxAttenuationDb), *m_logger);
    }
}

std::unique_ptr<PlutoDevice> PlutoDevice::Create(const PlutoConfig& config, std::shared_ptr<Logging::ILogger> logger)
{
    if (config.txAttenuationDb < minTxAttenuationDb || config.txAttenuationDb > maxTxAttenuationDb)
    {
        logger->Error(std::format("TX attenuation {} dB is out of the allowed range [{}, {}] dB",
                                  config.txAttenuationDb, minTxAttenuationDb, maxTxAttenuationDb));
        return nullptr;
    }

    auto device = std::unique_ptr<PlutoDevice>(new PlutoDevice(logger));
    device->m_context.reset(iio_create_context_from_uri(config.uri.c_str()));
    if (device->m_context == nullptr)
    {
        logger->Error(std::format("Cannot open Pluto at '{}': {}", config.uri, describeError(errno)));
        return nullptr;
    }

    const iio_context* context = device->m_context.get();
    const iio_device* phy = iio_context_find_device(context, "ad9361-phy");
    device->m_txDevice = iio_context_find_device(context, "cf-ad9361-dds-core-lpc");
    device->m_rxDevice = iio_context_find_device(context, "cf-ad9361-lpc");
    if (phy == nullptr || device->m_txDevice == nullptr || device->m_rxDevice == nullptr)
    {
        logger->Error(std::format("Device at '{}' has no AD936x IIO devices, is it a Pluto?", config.uri));
        return nullptr;
    }

    iio_channel* rxLo = iio_device_find_channel(phy, "altvoltage0", true);
    iio_channel* txLo = iio_device_find_channel(phy, "altvoltage1", true);
    iio_channel* rxPhy = iio_device_find_channel(phy, "voltage0", false);
    device->m_txGainChannel = iio_device_find_channel(phy, "voltage0", true);
    device->m_txI = iio_device_find_channel(device->m_txDevice, "voltage0", true);
    device->m_txQ = iio_device_find_channel(device->m_txDevice, "voltage1", true);
    device->m_rxI = iio_device_find_channel(device->m_rxDevice, "voltage0", false);
    device->m_rxQ = iio_device_find_channel(device->m_rxDevice, "voltage1", false);
    if (rxLo == nullptr || txLo == nullptr || rxPhy == nullptr || device->m_txGainChannel == nullptr ||
        device->m_txI == nullptr || device->m_txQ == nullptr || device->m_rxI == nullptr || device->m_rxQ == nullptr)
    {
        logger->Error(std::format("Pluto at '{}' is missing expected IIO channels", config.uri));
        return nullptr;
    }

    // Сначала минимальная мощность передатчика и ручное усиление приёмника, затем частоты
    iio_channel* txPhy = device->m_txGainChannel;
    const bool isConfigured = writeAttribute(txPhy, "hardwaregain", std::to_string(-maxTxAttenuationDb), *logger) &&
                              writeAttribute(rxPhy, "gain_control_mode", "manual", *logger) &&
                              writeAttribute(rxPhy, "hardwaregain", std::to_string(config.rxGainDb), *logger) &&
                              writeAttribute(rxPhy, "sampling_frequency", std::to_string(config.sampleRateHz), *logger) &&
                              writeAttribute(txPhy, "sampling_frequency", std::to_string(config.sampleRateHz), *logger) &&
                              writeAttribute(rxPhy, "rf_bandwidth", std::to_string(config.rfBandwidthHz), *logger) &&
                              writeAttribute(txPhy, "rf_bandwidth", std::to_string(config.rfBandwidthHz), *logger) &&
                              writeAttribute(rxLo, "frequency", std::to_string(config.centerFrequencyHz), *logger) &&
                              writeAttribute(txLo, "frequency", std::to_string(config.centerFrequencyHz), *logger) &&
                              writeAttribute(txPhy, "hardwaregain", std::to_string(-config.txAttenuationDb), *logger);
    if (!isConfigured)
    {
        return nullptr;
    }

    logger->Info(std::format("Pluto '{}': {} Hz, {} S/s, bandwidth {} Hz, TX attenuation {} dB, RX gain {} dB",
                             config.uri, config.centerFrequencyHz, config.sampleRateHz, config.rfBandwidthHz,
                             config.txAttenuationDb, config.rxGainDb));
    return device;
}

bool PlutoDevice::StartTransmit(std::span<const Sample_t> signal)
{
    StopTransmit();
    iio_channel_enable(m_txI);
    iio_channel_enable(m_txQ);
    m_txBuffer.reset(iio_device_create_buffer(m_txDevice, signal.size(), true));
    if (m_txBuffer == nullptr)
    {
        m_logger->Error(std::format("Cannot create TX buffer of {} samples: {}", signal.size(), describeError(errno)));
        return false;
    }

    // Каналы I и Q чередуются в буфере: I, Q, I, Q, ...
    const ptrdiff_t step = iio_buffer_step(m_txBuffer.get());
    auto* data = static_cast<std::byte*>(iio_buffer_first(m_txBuffer.get(), m_txI));
    for (const Sample_t& sample : signal)
    {
        auto* iq = reinterpret_cast<int16_t*>(data);
        iq[0] = toDacSample(sample.real());
        iq[1] = toDacSample(sample.imag());
        data += step;
    }

    const ssize_t pushed = iio_buffer_push(m_txBuffer.get());
    if (pushed < 0)
    {
        m_logger->Error(std::format("Cannot push TX buffer: {}", describeError(int(-pushed))));
        m_txBuffer.reset();
        return false;
    }
    m_logger->Info(std::format("Transmission started: {} samples repeated cyclically", signal.size()));
    return true;
}

void PlutoDevice::StopTransmit()
{
    if (m_txBuffer == nullptr)
    {
        return;
    }
    m_txBuffer.reset();
    m_logger->Info("Transmission stopped");
}

std::optional<Signal_t> PlutoDevice::Receive(size_t sampleCount)
{
    iio_channel_enable(m_rxI);
    iio_channel_enable(m_rxQ);
    const Buffer_t buffer(iio_device_create_buffer(m_rxDevice, sampleCount, false), iio_buffer_destroy);
    if (buffer == nullptr)
    {
        m_logger->Error(std::format("Cannot create RX buffer of {} samples: {}", sampleCount, describeError(errno)));
        return std::nullopt;
    }

    // Первый буфер отбрасываем: он мог начаться до того, как сигнал установился
    for (size_t i = 0; i < 2; ++i)
    {
        const ssize_t received = iio_buffer_refill(buffer.get());
        if (received < 0)
        {
            m_logger->Error(std::format("Cannot refill RX buffer: {}", describeError(int(-received))));
            return std::nullopt;
        }
    }

    Signal_t signal;
    signal.reserve(sampleCount);
    const ptrdiff_t step = iio_buffer_step(buffer.get());
    const auto* end = static_cast<const std::byte*>(iio_buffer_end(buffer.get()));
    for (auto* data = static_cast<const std::byte*>(iio_buffer_first(buffer.get(), m_rxI)); data < end; data += step)
    {
        const auto* iq = reinterpret_cast<const int16_t*>(data);
        signal.emplace_back(double(iq[0]) / rxFullScale, double(iq[1]) / rxFullScale);
    }
    m_logger->Trace(std::format("Received {} samples", signal.size()));
    return signal;
}

} // namespace Radiocom::Sdr
