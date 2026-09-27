#pragma once

#include "Logger.h"
#include "SignalManipulation.h"

#include <memory>
#include <span>
#include <string>


namespace Radiocom::Core
{

struct SimulationConfig
{
    std::string message = "testInfoRunSimulator!";
    double snrStartDb = -40.0;
    double snrStopDb = 40.0;
    double snrStepDb = 0.5;
};

class SimulationCore
{
public:
    static std::unique_ptr<SimulationCore> Create(SimulationConfig config,
                                                  std::unique_ptr<SignalManipulation::ITransmissionPipeline> pipeline,
                                                  std::shared_ptr<Logging::ILogger> logger);

    bool Run();

private:
    SimulationCore(SimulationConfig config,
                   std::unique_ptr<SignalManipulation::ITransmissionPipeline> pipeline,
                   std::shared_ptr<Logging::ILogger> logger);

    Bytes_t buildPayload() const;
    void logResults(std::span<const SignalManipulation::TransmissionReport> reports) const;

    SimulationConfig m_config;
    std::unique_ptr<SignalManipulation::ITransmissionPipeline> m_pipeline;
    std::shared_ptr<Logging::ILogger> m_logger;
};

} // namespace Radiocom::Core
