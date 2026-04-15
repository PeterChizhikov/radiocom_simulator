#pragma once

#include <memory>
#include <string>

class Logger {
public:
    static Logger &getInstance();

    void info(const std::string &message);
    void warn(const std::string &message);
    void error(const std::string &message);
    void debug(const std::string &message);

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

private:
    Logger();
    ~Logger();

    class Impl;
    Impl *impl;
};