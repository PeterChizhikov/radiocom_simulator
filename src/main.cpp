#include "SimulationCore.h"
#include "Logger.h"
#include "SignalFileIO.h"
#include "SignalManipulation.h"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>


namespace
{

namespace Core = Radiocom::Core;
namespace Logging = Radiocom::Logging;
namespace SignalFileIO = Radiocom::SignalFileIO;
namespace SignalManipulation = Radiocom::SignalManipulation;

struct CommandLineOptions
{
    bool isHelpRequested = false;
    bool isVerbose = false;
    std::optional<uint32_t> seed;
    // Этот файл читает флоуграф GNU Radio из tests/gnu_radio
    std::string outputPath = "test";
};

void printUsage(std::string_view programName)
{
    std::cout << "Usage: " << programName << " [options]\n"
              << "  -v, --verbose      enable trace logging\n"
              << "  --seed <n>         seed for the AWGN channel (random by default)\n"
              << "  --output <path>    file for the transmitted I/Q signal (default: test)\n"
              << "  -h, --help         show this help\n";
}

std::optional<uint32_t> parseSeed(std::string_view text)
{
    uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

std::optional<CommandLineOptions> parseCommandLine(std::span<char*> args)
{
    CommandLineOptions options;
    for (size_t i = 1; i < args.size(); ++i)
    {
        const std::string_view arg = args[i];
        const bool hasValue = i + 1 < args.size();

        if (arg == "-h" || arg == "--help")
        {
            options.isHelpRequested = true;
            continue;
        }
        if (arg == "-v" || arg == "--verbose")
        {
            options.isVerbose = true;
            continue;
        }
        if (arg == "--seed" && hasValue)
        {
            options.seed = parseSeed(args[++i]);
            if (!options.seed.has_value())
            {
                std::cerr << "Invalid seed: " << args[i] << '\n';
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--output" && hasValue)
        {
            options.outputPath = args[++i];
            continue;
        }

        std::cerr << "Unknown or incomplete option: " << arg << '\n';
        return std::nullopt;
    }
    return options;
}

int runSimulation(const CommandLineOptions& options)
{
    const auto logger =
        Logging::createConsoleLogger(options.isVerbose ? Logging::LogLevel::Trace : Logging::LogLevel::Info);
    if (logger == nullptr)
    {
        return EXIT_FAILURE;
    }

    auto storage = std::make_shared<SignalFileIO::BinaryFileSignalStorage>(options.outputPath, logger);

    SignalManipulation::PipelineConfig pipelineConfig;
    pipelineConfig.channelSeed = options.seed;
    auto pipeline = SignalManipulation::createTransmissionPipeline(pipelineConfig, std::move(storage), logger);
    if (pipeline == nullptr)
    {
        return EXIT_FAILURE;
    }

    const auto simulation = Core::SimulationCore::Create(Core::SimulationConfig(), std::move(pipeline), logger);
    if (simulation == nullptr)
    {
        return EXIT_FAILURE;
    }
    return simulation->Run() ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv)
{
    const std::span<char*> args(argv, size_t(argc));
    const auto options = parseCommandLine(args);
    if (!options.has_value())
    {
        printUsage(args[0]);
        return EXIT_FAILURE;
    }
    if (options->isHelpRequested)
    {
        printUsage(args[0]);
        return EXIT_SUCCESS;
    }

    try
    {
        return runSimulation(*options);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Simulation terminated by an unhandled exception: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
