#include "SimulationCore.h"

#include <cmath>
#include <format>
#include <string_view>
#include <utility>
#include <vector>


namespace Radiocom::Core
{
namespace
{

std::string payloadToText(std::span<const uint8_t> payload)
{
    std::string text(payload.begin(), payload.end());
    while (!text.empty() && text.back() == '\0')
    {
        text.pop_back();
    }
    return text;
}

} // namespace

std::unique_ptr<SimulationCore> SimulationCore::Create(SimulationConfig config,
                                                       std::unique_ptr<SignalManipulation::ITransmissionPipeline> pipeline,
                                                       std::shared_ptr<Logging::ILogger> logger)
{
    if (logger == nullptr)
    {
        return nullptr;
    }
    if (pipeline == nullptr)
    {
        logger->Error("Cannot create simulation: transmission pipeline is not provided");
        return nullptr;
    }
    if (!(config.snrStepDb > 0.0) || !(config.snrStartDb < config.snrStopDb))
    {
        logger->Error(std::format("Cannot create simulation: invalid SNR range [{}, {}) with step {}", config.snrStartDb,
                                  config.snrStopDb, config.snrStepDb));
        return nullptr;
    }

    return std::unique_ptr<SimulationCore>(new SimulationCore(std::move(config), std::move(pipeline), std::move(logger)));
}

SimulationCore::SimulationCore(SimulationConfig config,
                               std::unique_ptr<SignalManipulation::ITransmissionPipeline> pipeline,
                               std::shared_ptr<Logging::ILogger> logger)
    : m_config(std::move(config))
    , m_pipeline(std::move(pipeline))
    , m_logger(std::move(logger))
{
}

bool SimulationCore::Run()
{
    const Bytes_t payload = buildPayload();
    const auto pointCount = size_t(std::ceil((m_config.snrStopDb - m_config.snrStartDb) / m_config.snrStepDb));

    m_logger->Info(std::format("Simulation started: {} SNR points, payload {} bytes", pointCount, payload.size()));

    std::vector<SignalManipulation::TransmissionReport> reports;
    reports.reserve(pointCount);
    for (size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex)
    {
        const double snrDb = m_config.snrStartDb + double(pointIndex) * m_config.snrStepDb;
        auto report = m_pipeline->Run(payload, snrDb);
        if (!report.has_value())
        {
            m_logger->Error(std::format("Simulation aborted at SNR {:f} dB", snrDb));
            return false;
        }

        if (report->isDecoded)
        {
            m_logger->Trace(std::format("Decoded text: {}", payloadToText(report->decodedPayload)));
        }
        reports.push_back(std::move(*report));
    }

    logResults(reports);
    return true;
}

Bytes_t SimulationCore::buildPayload() const
{
    // Нуль-терминатор передаётся вместе с сообщением
    Bytes_t payload(m_config.message.begin(), m_config.message.end());
    payload.push_back(0);
    return payload;
}

void SimulationCore::logResults(std::span<const SignalManipulation::TransmissionReport> reports) const
{
    // Формат строки разбирает tests/plotBER_SBR.py, не менять
    m_logger->Info("==================Result block==================");
    for (const auto& report : reports)
    {
        const std::string_view status = report.isDecoded ? "success" : "error";
        m_logger->Info(std::format("snr: {:f}, BER: {:f}, status: {}", report.snrDb, report.bitErrorRate, status));
    }
}

} // namespace Radiocom::Core
