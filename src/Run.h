#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>


namespace Radiocom::Launcher
{

enum class Role
{
    BaseStation,
    Subscriber,
};

struct CommandLineOptions
{
    bool isHelpRequested = false;
    bool isVerbose = false;
    std::optional<Role> role;
    std::string uri;
    double txAttenuationDb = 89.75;
    double rxGainDb = 0.0;
    std::string outputPath = "rx.iq";
};

void printUsage(std::string_view programName);
// При ошибке пишет причину в stderr и возвращает std::nullopt
std::optional<CommandLineOptions> parseCommandLine(std::span<char*> args);

// Создаёт логгер, Pluto и физический уровень и выполняет роль; возвращает код завершения процесса
int run(const CommandLineOptions& options);

} // namespace Radiocom::Launcher
