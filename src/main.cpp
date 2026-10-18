#include "Logger.h"
#include "Sdr.h"
#include "SignalFileIO.h"
#include "SignalManipulation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>


namespace
{

namespace Logging = Radiocom::Logging;
namespace Sdr = Radiocom::Sdr;
namespace SignalFileIO = Radiocom::SignalFileIO;
namespace SignalManipulation = Radiocom::SignalManipulation;

constexpr std::string_view testMessage = "radiocom-test-frame-1";
constexpr size_t receiveSize = 65536;
constexpr double txPeakAmplitude = 0.5;

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

void printUsage(std::string_view programName)
{
    std::cout << "Usage: " << programName << " --role <bs|ue> --uri <uri> [options]\n"
              << "  --role <bs|ue>       bs - base station (transmits), ue - subscriber (receives)\n"
              << "  --uri <uri>          Pluto of this role, e.g. usb:1.2.5\n"
              << "  --tx-atten <db>      TX attenuation, 40..89.75 dB (default: 89.75)\n"
              << "  --rx-gain <db>       manual RX gain, -3..71 dB (default: 0)\n"
              << "  --output <path>      file for the received I/Q signal (default: rx.iq)\n"
              << "  -v, --verbose        enable trace logging\n"
              << "  -h, --help           show this help\n";
}

std::optional<double> parseDouble(const char* text)
{
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0')
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
        if (arg == "--role" && hasValue)
        {
            const std::string_view value = args[++i];
            if (value == "bs")
            {
                options.role = Role::BaseStation;
            }
            else if (value == "ue")
            {
                options.role = Role::Subscriber;
            }
            else
            {
                std::cerr << "Unknown role: " << value << '\n';
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--uri" && hasValue)
        {
            options.uri = args[++i];
            continue;
        }
        if (arg == "--output" && hasValue)
        {
            options.outputPath = args[++i];
            continue;
        }
        if ((arg == "--tx-atten" || arg == "--rx-gain") && hasValue)
        {
            const auto value = parseDouble(args[++i]);
            if (!value.has_value())
            {
                std::cerr << "Invalid number for " << arg << ": " << args[i] << '\n';
                return std::nullopt;
            }
            (arg == "--tx-atten" ? options.txAttenuationDb : options.rxGainDb) = *value;
            continue;
        }

        std::cerr << "Unknown or incomplete option: " << arg << '\n';
        return std::nullopt;
    }

    if (!options.isHelpRequested && (!options.role.has_value() || options.uri.empty()))
    {
        std::cerr << "--role and --uri are required\n";
        return std::nullopt;
    }
    return options;
}

// Тестовое сообщение повторяется, пока не заполнит слот
Radiocom::Bytes_t buildPayload(size_t size)
{
    Radiocom::Bytes_t payload(size);
    for (size_t index = 0; index < size; ++index)
    {
        payload[index] = uint8_t(testMessage[index % testMessage.size()]);
    }
    return payload;
}

// Базовая станция передаёт кадр циклически, пока не нажат Enter
int runBaseStation(Sdr::PlutoDevice& device, SignalManipulation::IPhyLayer& phy, Logging::ILogger& logger)
{
    auto frame = phy.Modulate(buildPayload(phy.GetPayloadSize()));
    if (!frame.has_value())
    {
        return EXIT_FAILURE;
    }

    // Кадр из модулятора слабый (около -30 dBFS): поднимаем пик до уровня тона из radiocom_txrx
    double peak = 0.0;
    for (const auto& sample : *frame)
    {
        peak = std::max({peak, std::abs(sample.real()), std::abs(sample.imag())});
    }
    for (auto& sample : *frame)
    {
        sample *= txPeakAmplitude / peak;
    }
    if (!device.StartTransmit(*frame))
    {
        return EXIT_FAILURE;
    }

    logger.Info(std::format("Transmitting a frame of {} samples, press Enter to stop", frame->size()));
    std::cin.get();
    device.StopTransmit();
    return EXIT_SUCCESS;
}

void logSignalLevel(const Radiocom::Signal_t& signal, Logging::ILogger& logger)
{
    double meanPower = 0.0;
    double maxComponent = 0.0;
    for (const auto& sample : signal)
    {
        meanPower += std::norm(sample);
        maxComponent = std::max({maxComponent, std::abs(sample.real()), std::abs(sample.imag())});
    }
    meanPower /= double(signal.size());
    logger.Info(std::format("RX level: {:.1f} dBFS, max |I|,|Q| = {:.3f} of full scale{}",
                            10.0 * std::log10(meanPower), maxComponent,
                            maxComponent >= 0.999 ? " — CLIPPING, lower the RX gain" : ""));
}

// Число ошибочных кодовых бит до декодирования; эталон получаем демодуляцией кадра без шума
void logBitErrors(SignalManipulation::IPhyLayer& phy,
                  std::span<const Radiocom::Sample_t> signal,
                  const SignalManipulation::SyncPeak& peak,
                  Logging::ILogger& logger)
{
    const Radiocom::Bytes_t payload = buildPayload(phy.GetPayloadSize());
    const auto cleanFrame = phy.Modulate(payload);
    if (!cleanFrame.has_value())
    {
        return;
    }
    const auto reference = phy.Demodulate(*cleanFrame, 0.0);
    const auto received = phy.Demodulate(signal.subspan(peak.position), peak.frequencyOffsetHz);
    if (!reference.has_value() || !received.has_value())
    {
        return;
    }

    size_t errorCount = 0;
    for (size_t index = 0; index < reference->codedBits.size(); ++index)
    {
        errorCount += reference->codedBits[index] != received->codedBits[index] ? 1 : 0;
    }
    const bool isMessageCorrect = received->payload.has_value() && *received->payload == payload;
    logger.Info(std::format("Coded bit errors before decoding: {} of {}, CRC ok and message correct: {}", errorCount,
                            reference->codedBits.size(), isMessageCorrect));
}

// Абонент принимает окно сигнала и сохраняет его для разбора
int runSubscriber(Sdr::PlutoDevice& device,
                  SignalManipulation::IPhyLayer& phy,
                  const CommandLineOptions& options,
                  std::shared_ptr<Logging::ILogger> logger)
{
    const auto received = device.Receive(receiveSize);
    if (!received.has_value())
    {
        return EXIT_FAILURE;
    }

    logSignalLevel(*received, *logger);
    if (const auto peak = phy.FindSync(*received); peak.has_value())
    {
        logger->Info(std::format("Slot starts at sample {}, correlation {:.3f}, frequency offset {:.0f} Hz",
                                 peak->position, peak->correlation, peak->frequencyOffsetHz));
        logBitErrors(phy, *received, *peak, *logger);
    }
    SignalFileIO::BinaryFileSignalStorage storage(options.outputPath, std::move(logger));
    return storage.Save(*received) ? EXIT_SUCCESS : EXIT_FAILURE;
}

int run(const CommandLineOptions& options)
{
    const auto logger =
        Logging::createConsoleLogger(options.isVerbose ? Logging::LogLevel::Trace : Logging::LogLevel::Info);
    if (logger == nullptr)
    {
        return EXIT_FAILURE;
    }

    Sdr::PlutoConfig plutoConfig;
    plutoConfig.uri = options.uri;
    plutoConfig.txAttenuationDb = options.txAttenuationDb;
    plutoConfig.rxGainDb = options.rxGainDb;
    const auto device = Sdr::PlutoDevice::Create(plutoConfig, logger);
    if (device == nullptr)
    {
        return EXIT_FAILURE;
    }

    const auto phy = SignalManipulation::createPhyLayer(SignalManipulation::PhyConfig(), logger);
    if (phy == nullptr)
    {
        return EXIT_FAILURE;
    }

    return *options.role == Role::BaseStation ? runBaseStation(*device, *phy, *logger)
                                              : runSubscriber(*device, *phy, options, logger);
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
        return run(*options);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Terminated by an unhandled exception: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
