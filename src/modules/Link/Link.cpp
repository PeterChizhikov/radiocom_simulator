#include "Link.h"
#include "LinkPacket.h"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <random>
#include <thread>
#include <utility>
#include <vector>


namespace Radiocom::Link
{
namespace
{

using Clock_t = std::chrono::steady_clock;

constexpr double txPeakAmplitude = 0.5;
volatile std::sig_atomic_t stopRequested = 0;

void requestStop(int)
{
    stopRequested = 1;
}

bool waitFor(std::chrono::milliseconds duration)
{
    const auto deadline = Clock_t::now() + duration;
    while (stopRequested == 0 && Clock_t::now() < deadline)
    {
        std::this_thread::sleep_until(std::min(deadline, Clock_t::now() + std::chrono::milliseconds(20)));
    }
    return stopRequested == 0;
}

bool transmitPacket(Sdr::PlutoDevice& device, SignalManipulation::IPhyLayer& phy, const Packet& packet,
                    std::chrono::milliseconds duration, Logging::ILogger& logger)
{
    const auto bytes = encodePacket(packet);
    if (bytes.size() != phy.GetPayloadSize())
    {
        logger.Error("Link packet does not fit the PHY transport block");
        return false;
    }
    const std::vector<Bytes_t> payloads(phy.GetDataSubframes().size(), bytes);
    auto frame = phy.Modulate(payloads);
    if (!frame.has_value())
    {
        return false;
    }
    double peak = 0.0;
    for (const auto& sample : *frame)
    {
        peak = std::max({peak, std::abs(sample.real()), std::abs(sample.imag())});
    }
    if (!std::isfinite(peak) || peak <= 0.0)
    {
        logger.Error("Cannot normalize an empty or invalid link frame");
        return false;
    }
    for (auto& sample : *frame)
    {
        sample *= txPeakAmplitude / peak;
    }
    if (!device.StartTransmit(*frame))
    {
        return false;
    }
    const bool completed = waitFor(duration);
    device.StopTransmit();
    return completed;
}

std::optional<std::vector<Packet>> receivePackets(Sdr::PlutoDevice& device, SignalManipulation::IPhyLayer& phy,
                                                 Logging::ILogger& logger)
{
    const auto signal = device.Receive(2 * phy.GetFrameLength());
    if (!signal.has_value())
    {
        return std::nullopt;
    }
    double peak = 0.0;
    for (const auto& sample : *signal)
    {
        peak = std::max({peak, std::abs(sample.real()), std::abs(sample.imag())});
    }
    if (peak >= 0.999)
    {
        logger.Warn("RX clipping during exchange; lower RX gain");
    }
    std::vector<Packet> packets;
    const auto sync = phy.FindSync(*signal);
    if (!sync.has_value())
    {
        return packets;
    }
    const auto payload = phy.DemodulateRepeatedPayload(std::span(*signal).subspan(sync->position),
                                                      sync->frequencyOffsetHz);
    if (!payload.has_value())
    {
        return packets;
    }
    if (auto packet = decodePacket(*payload); packet.has_value())
    {
        packets.push_back(std::move(*packet));
    }
    if (!packets.empty())
    {
        logger.Debug(std::format("Link RX: payload CRC ok, correlation {:.3f}, offset {:.0f} Hz, peak {:.3f}",
                                  sync->correlation, sync->frequencyOffsetHz, peak));
    }
    return packets;
}

uint64_t createSession()
{
    std::random_device source;
    return (uint64_t(source()) << 32) | uint64_t(source());
}

bool runBaseStation(Sdr::PlutoDevice& device, SignalManipulation::IPhyLayer& phy, const LinkConfig& config,
                    Logging::ILogger& logger)
{
    const uint64_t session = createSession();
    size_t completed = 0;
    size_t attempts = 0;
    const auto started = Clock_t::now();
    logger.Info(std::format("Link session {}: {} echo exchanges", session, config.exchangeCount));
    for (size_t index = 0; index < config.exchangeCount; ++index)
    {
        Packet request{PacketType::Request, session, uint32_t(index), Bytes_t(phy.GetPayloadSize() - packetHeaderSize)};
        for (size_t byte = 0; byte < request.data.size(); ++byte)
        {
            request.data[byte] = uint8_t((index + byte) % 256);
        }
        bool receivedEcho = false;
        for (size_t attempt = 0; attempt < config.maxAttempts && !receivedEcho && stopRequested == 0; ++attempt)
        {
            ++attempts;
            const auto requestStarted = Clock_t::now();
            logger.Info(std::format("Link request {}: attempt {}", index, attempt + 1));
            if (!transmitPacket(device, phy, request, config.transmitDuration, logger))
            {
                return false;
            }
            const auto deadline = Clock_t::now() + config.responseTimeout;
            while (Clock_t::now() < deadline && !receivedEcho && stopRequested == 0)
            {
                const auto packets = receivePackets(device, phy, logger);
                if (!packets.has_value())
                {
                    return false;
                }
                if (Clock_t::now() >= deadline)
                {
                    break;
                }
                receivedEcho = std::any_of(packets->begin(), packets->end(), [&request](const Packet& packet)
                {
                    return isEchoResponse(request, packet);
                });
            }
            if (receivedEcho)
            {
                ++completed;
                const double milliseconds = std::chrono::duration<double, std::milli>(Clock_t::now() -
                                                                                      requestStarted).count();
                logger.Info(std::format("Link echo {}: CRC and data correct, round trip {:.1f} ms",
                                         index, milliseconds));
            }
            else
            {
                logger.Warn(std::format("Link request {}: response timeout", index));
            }
            if (!waitFor(config.turnaroundDelay))
            {
                return false;
            }
        }
        if (!receivedEcho)
        {
            logger.Error(std::format("Link failed: {} of {} exchanges complete, {} attempts", completed,
                                     config.exchangeCount, attempts));
            return false;
        }
    }
    const double seconds = std::chrono::duration<double>(Clock_t::now() - started).count();
    logger.Info(std::format("Link complete: {} of {} echoes correct, {} attempts, {:.2f} seconds", completed,
                             config.exchangeCount, attempts, seconds));
    return true;
}

bool runSubscriber(Sdr::PlutoDevice& device, SignalManipulation::IPhyLayer& phy, const LinkConfig& config,
                   Logging::ILogger& logger)
{
    std::optional<Packet> lastRequest;
    size_t uniqueRequests = 0;
    size_t responses = 0;
    auto deadline = Clock_t::now() + config.idleTimeout;
    logger.Info("Link subscriber: waiting for requests");
    while (Clock_t::now() < deadline && stopRequested == 0)
    {
        const auto packets = receivePackets(device, phy, logger);
        if (!packets.has_value())
        {
            return false;
        }
        const Packet* request = nullptr;
        for (const auto& packet : *packets)
        {
            if (packet.type != PacketType::Request)
            {
                continue;
            }
            if (!lastRequest.has_value())
            {
                if (packet.sequence == 0)
                {
                    request = &packet;
                    break;
                }
                continue;
            }
            if (packet.session != lastRequest->session)
            {
                continue;
            }
            const bool isDuplicate = packet.sequence == lastRequest->sequence && packet.data == lastRequest->data;
            const bool isNext = uniqueRequests < config.exchangeCount &&
                                uint64_t(packet.sequence) == uint64_t(lastRequest->sequence) + 1;
            if (isDuplicate || isNext)
            {
                request = &packet;
                break;
            }
        }
        if (request == nullptr)
        {
            continue;
        }
        const bool isDuplicate = lastRequest.has_value() && request->sequence == lastRequest->sequence;
        if (isDuplicate)
        {
            logger.Debug(std::format("Ignoring duplicate request {}: retransmissions are disabled", request->sequence));
            continue;
        }
        lastRequest = *request;
        ++uniqueRequests;
        logger.Info(std::format("Link request {} received: session {}, new", request->sequence, request->session));
        if (!waitFor(config.turnaroundDelay))
        {
            return false;
        }
        auto response = *request;
        response.type = PacketType::Response;
        if (!transmitPacket(device, phy, response, config.transmitDuration, logger))
        {
            return false;
        }
        ++responses;
        deadline = Clock_t::now() + config.idleTimeout;
    }
    logger.Info(std::format("Link subscriber idle: {} unique requests, {} responses", uniqueRequests, responses));
    return stopRequested == 0 && uniqueRequests == config.exchangeCount;
}

} // namespace

bool runExchange(Sdr::PlutoDevice& device, SignalManipulation::IPhyLayer& phy, const LinkConfig& config,
                 Logging::ILogger& logger)
{
    if (phy.GetPayloadSize() <= packetHeaderSize || config.exchangeCount == 0 || config.maxAttempts != 1 ||
        config.exchangeCount > std::numeric_limits<uint32_t>::max() || config.transmitDuration.count() <= 0 ||
        config.turnaroundDelay <= config.transmitDuration || config.responseTimeout <= config.turnaroundDelay +
        config.transmitDuration || config.idleTimeout <= config.responseTimeout)
    {
        logger.Error("Invalid link configuration or PHY payload size");
        return false;
    }
    stopRequested = 0;
    const auto previousInterrupt = std::signal(SIGINT, requestStop);
    if (previousInterrupt == SIG_ERR)
    {
        logger.Error("Cannot install interrupt handler");
        return false;
    }
    const auto previousTerminate = std::signal(SIGTERM, requestStop);
    if (previousTerminate == SIG_ERR)
    {
        std::signal(SIGINT, previousInterrupt);
        logger.Error("Cannot install termination handler");
        return false;
    }
    const bool completed = config.role == Role::BaseStation ? runBaseStation(device, phy, config, logger)
                                                            : runSubscriber(device, phy, config, logger);
    device.StopTransmit();
    std::signal(SIGINT, previousInterrupt);
    std::signal(SIGTERM, previousTerminate);
    return completed;
}

} // namespace Radiocom::Link
