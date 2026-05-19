#include "Logger.h"
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

class Logger::Impl {
public:
    Impl() {
        logger = spdlog::stdout_color_mt("calculator_logger");
        logger->set_pattern("[%H:%M:%S] [%^%l%$] %v");
        logger->set_level(spdlog::level::debug);
    }

    std::shared_ptr<spdlog::logger> logger;
};

Logger &Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger() : impl(new Impl()) {}

Logger::~Logger() {
    delete impl;
}

void Logger::info(const std::string &message) {
    impl->logger->info(message);
}

void Logger::warn(const std::string &message) {
    impl->logger->warn(message);
}

void Logger::error(const std::string &message) {
    impl->logger->error(message);
}

void Logger::debug(const std::string &message) {
    impl->logger->debug(message);
}