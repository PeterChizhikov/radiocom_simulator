#pragma once

#include <memory>
#include <string_view>


namespace Radiocom::Logging
{

enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warn,
    Error
};

class ILogger
{
public:
    virtual ~ILogger() = default;

    virtual void Log(LogLevel level, std::string_view message) = 0;

    void Trace(std::string_view message) { Log(LogLevel::Trace, message); }
    void Debug(std::string_view message) { Log(LogLevel::Debug, message); }
    void Info(std::string_view message) { Log(LogLevel::Info, message); }
    void Warn(std::string_view message) { Log(LogLevel::Warn, message); }
    void Error(std::string_view message) { Log(LogLevel::Error, message); }
};

std::shared_ptr<ILogger> createConsoleLogger(LogLevel minLevel);

} // namespace Radiocom::Logging
