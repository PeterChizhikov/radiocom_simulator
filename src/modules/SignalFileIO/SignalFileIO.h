#pragma once

#include "Logger.h"
#include "Types.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>


namespace Radiocom::SignalFileIO
{

class ISignalStorage
{
public:
    virtual ~ISignalStorage() = default;

    virtual bool Save(std::span<const Sample_t> signal) = 0;
    virtual std::optional<Signal_t> Load() = 0;
};

// Формат файла: чередующиеся float32 I/Q (gr_complex в GNU Radio)
class BinaryFileSignalStorage final : public ISignalStorage
{
public:
    explicit BinaryFileSignalStorage(std::filesystem::path filePath, std::shared_ptr<Logging::ILogger> logger);

    bool Save(std::span<const Sample_t> signal) override;
    std::optional<Signal_t> Load() override;

private:
    std::filesystem::path m_filePath;
    std::shared_ptr<Logging::ILogger> m_logger;
};

} // namespace Radiocom::SignalFileIO
