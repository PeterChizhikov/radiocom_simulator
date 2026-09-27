#pragma once

#include "Logger.h"
#include "SignalFileIO.h"
#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>


namespace Radiocom::SignalManipulation
{

struct TransmissionReport
{
    double snrDb = 0.0;
    // BER до исправления ошибок кодом Хэмминга
    double bitErrorRate = 0.0;
    bool isDecoded = false;
    Bytes_t decodedPayload;
};

class ITransmissionPipeline
{
public:
    virtual ~ITransmissionPipeline() = default;

    virtual std::optional<TransmissionReport> Run(std::span<const uint8_t> payload, double snrDb) = 0;
};

struct PipelineConfig
{
    size_t fftSize = 256;
    size_t cyclicPrefixSize = 18;
    size_t subcarriersPerSide = 64;
    std::optional<uint32_t> channelSeed;
};

std::unique_ptr<ITransmissionPipeline> createTransmissionPipeline(const PipelineConfig& config,
                                                                  std::shared_ptr<SignalFileIO::ISignalStorage> storage,
                                                                  std::shared_ptr<Logging::ILogger> logger);

} // namespace Radiocom::SignalManipulation
