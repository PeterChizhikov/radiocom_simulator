#include "Logger.h"

#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>


namespace Radiocom::Logging
{
namespace
{

constexpr std::string_view loggerName = "radiocom";
constexpr std::string_view logPattern = "[%H:%M:%S] [%^%l%$] %v";

spdlog::level::level_enum toSpdlogLevel(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warn:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    }
    return spdlog::level::info;
}

class SpdlogConsoleLogger final : public ILogger
{
public:
    explicit SpdlogConsoleLogger(std::shared_ptr<spdlog::logger> logger)
        : m_logger(std::move(logger))
    {
    }

    void Log(LogLevel level, std::string_view message) override
    {
        m_logger->log(toSpdlogLevel(level), message);
    }

private:
    std::shared_ptr<spdlog::logger> m_logger;
};

} // namespace

std::shared_ptr<ILogger> createConsoleLogger(LogLevel minLevel)
{
    try
    {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>(std::string(loggerName), std::move(sink));
        logger->set_pattern(std::string(logPattern));
        logger->set_level(toSpdlogLevel(minLevel));
        return std::make_shared<SpdlogConsoleLogger>(std::move(logger));
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to create console logger: " << e.what() << '\n';
        return nullptr;
    }
}

} // namespace Radiocom::Logging
