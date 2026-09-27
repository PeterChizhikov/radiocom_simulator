#include "SignalFileIO.h"

#include <cerrno>
#include <format>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>


namespace Radiocom::SignalFileIO
{
namespace
{

constexpr size_t componentsPerSample = 2;
constexpr size_t bytesPerSample = componentsPerSample * sizeof(float);

std::string describeLastError()
{
    return std::error_code(errno, std::generic_category()).message();
}

} // namespace

BinaryFileSignalStorage::BinaryFileSignalStorage(std::filesystem::path filePath,
                                                 std::shared_ptr<Logging::ILogger> logger)
    : m_filePath(std::move(filePath))
    , m_logger(std::move(logger))
{
}

bool BinaryFileSignalStorage::Save(std::span<const Sample_t> signal)
{
    std::vector<float> interleaved;
    interleaved.reserve(signal.size() * componentsPerSample);
    for (const auto& sample : signal)
    {
        interleaved.push_back(float(sample.real()));
        interleaved.push_back(float(sample.imag()));
    }

    std::ofstream file(m_filePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        m_logger->Error(std::format("Failed to open '{}' for writing: {}", m_filePath.string(), describeLastError()));
        return false;
    }

    file.write(reinterpret_cast<const char*>(interleaved.data()),
               std::streamsize(interleaved.size() * sizeof(float)));
    if (file.fail())
    {
        m_logger->Error(std::format("Failed to write {} samples to '{}': {}", signal.size(), m_filePath.string(),
                                    describeLastError()));
        return false;
    }

    m_logger->Trace(std::format("Signal saved to '{}' ({} samples)", m_filePath.string(), signal.size()));
    return true;
}

std::optional<Signal_t> BinaryFileSignalStorage::Load()
{
    std::ifstream file(m_filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        m_logger->Error(std::format("Failed to open '{}' for reading: {}", m_filePath.string(), describeLastError()));
        return std::nullopt;
    }

    const std::streamoff fileSize = file.tellg();
    if (fileSize < 0)
    {
        m_logger->Error(std::format("Failed to determine size of '{}'", m_filePath.string()));
        return std::nullopt;
    }
    file.seekg(0);

    const size_t sampleCount = size_t(fileSize) / bytesPerSample;
    std::vector<float> interleaved(sampleCount * componentsPerSample);
    file.read(reinterpret_cast<char*>(interleaved.data()), std::streamsize(interleaved.size() * sizeof(float)));
    if (file.fail())
    {
        m_logger->Error(std::format("Failed to read {} samples from '{}': {}", sampleCount, m_filePath.string(),
                                    describeLastError()));
        return std::nullopt;
    }

    Signal_t signal;
    signal.reserve(sampleCount);
    for (size_t i = 0; i < interleaved.size(); i += componentsPerSample)
    {
        signal.emplace_back(double(interleaved[i]), double(interleaved[i + 1]));
    }

    m_logger->Trace(std::format("Signal loaded from '{}' ({} samples)", m_filePath.string(), signal.size()));
    return signal;
}

} // namespace Radiocom::SignalFileIO
