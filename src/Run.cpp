#include "Run.h"

#include "Logger.h"
#include "Link.h"
#include "Sdr.h"
#include "SignalFileIO.h"
#include "SignalManipulation.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <string>
#include <vector>


namespace Radiocom::Launcher
{
namespace
{

constexpr size_t receivedFrames = 2;
constexpr double txPeakAmplitude = 0.5;

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

std::optional<size_t> parseSize(const char* text)
{
    char* end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0' || text[0] == '-' || errno == ERANGE ||
        value > std::numeric_limits<size_t>::max())
    {
        return std::nullopt;
    }
    return size_t(value);
}

// Тестовое сообщение с номером субкадра повторяется, пока не заполнит транспортный блок
Radiocom::Bytes_t buildPayload(size_t size, size_t subframe)
{
    const std::string message = std::format("radiocom subframe {} ", subframe);
    Radiocom::Bytes_t payload(size);
    for (size_t index = 0; index < size; ++index)
    {
        payload[index] = uint8_t(message[index % message.size()]);
    }
    return payload;
}

std::vector<Radiocom::Bytes_t> buildPayloads(const SignalManipulation::IPhyLayer& phy)
{
    std::vector<Radiocom::Bytes_t> payloads;
    for (const size_t subframe : phy.GetDataSubframes())
    {
        payloads.push_back(buildPayload(phy.GetPayloadSize(), subframe));
    }
    return payloads;
}

// Базовая станция передаёт кадр циклически, пока не нажат Enter
int runBaseStation(Sdr::PlutoDevice& device, SignalManipulation::IPhyLayer& phy, Logging::ILogger& logger)
{
    auto frame = phy.Modulate(buildPayloads(phy));
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

    logger.Info(std::format("Transmitting a frame of {} samples with {} transport blocks of {} bytes, "
                            "press Enter to stop",
                            frame->size(), phy.GetDataSubframes().size(), phy.GetPayloadSize()));
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

// По каждому субкадру: число ошибочных кодовых бит до декодирования (эталон — демодуляция кадра без шума) и верно ли
// принято сообщение
void logFrameResults(SignalManipulation::IPhyLayer& phy,
                     std::span<const Radiocom::Sample_t> signal,
                     const SignalManipulation::SyncPeak& peak,
                     Logging::ILogger& logger)
{
    const auto payloads = buildPayloads(phy);
    const auto cleanFrame = phy.Modulate(payloads);
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

    size_t correctBlocks = 0;
    for (size_t index = 0; index < received->size(); ++index)
    {
        const auto& expected = (*reference)[index];
        const auto& actual = (*received)[index];
        size_t errorCount = 0;
        for (size_t bit = 0; bit < expected.codedBits.size(); ++bit)
        {
            errorCount += expected.codedBits[bit] != actual.codedBits[bit] ? 1 : 0;
        }
        const bool isMessageCorrect = actual.payload.has_value() && *actual.payload == payloads[index];
        correctBlocks += isMessageCorrect ? 1 : 0;
        logger.Info(std::format("Subframe {}: coded bit errors before decoding {} of {} ({:.2f} %), "
                                "CRC ok and message correct: {}",
                                actual.subframe, errorCount, expected.codedBits.size(),
                                100.0 * double(errorCount) / double(expected.codedBits.size()), isMessageCorrect));
    }
    logger.Info(std::format("Frame: {} of {} transport blocks correct, {} payload bits per 10 ms", correctBlocks,
                            received->size(), correctBlocks * phy.GetPayloadSize() * 8));
}

// Абонент принимает окно сигнала и сохраняет его для разбора
int runSubscriber(Sdr::PlutoDevice& device,
                  SignalManipulation::IPhyLayer& phy,
                  const CommandLineOptions& options,
                  std::shared_ptr<Logging::ILogger> logger)
{
    // Окно из двух кадров всегда содержит целый кадр
    const auto received = device.Receive(receivedFrames * phy.GetFrameLength());
    if (!received.has_value())
    {
        return EXIT_FAILURE;
    }

    logSignalLevel(*received, *logger);
    if (const auto peak = phy.FindSync(*received); peak.has_value())
    {
        logger->Info(std::format("Frame starts at sample {}, PSS correlation {:.3f}, frequency offset {:.0f} Hz",
                                 peak->position, peak->correlation, peak->frequencyOffsetHz));
        logFrameResults(phy, *received, *peak, *logger);
    }
    SignalFileIO::BinaryFileSignalStorage storage(options.outputPath, std::move(logger));
    return storage.Save(*received) ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

void printUsage(std::string_view programName)
{
    std::cout << "Usage: " << programName << " --role <bs|ue> --uri <uri> [options]\n"
              << "  --role <bs|ue>       bs - base station (transmits), ue - subscriber (receives)\n"
              << "  --uri <uri>          Pluto of this role, e.g. usb:1.2.5\n"
              << "  --tx-atten <db>      TX attenuation, 0..89.75 dB (default: 89.75)\n"
              << "  --rx-gain <db>       manual RX gain, -3..71 dB (default: 0)\n"
              << "  --mcs <index>        MCS index 0..28, the same on both sides (default: 0)\n"
              << "  --exchange           automatic TDD echo exchange instead of one-way operation\n"
              << "  --count <number>     number of echo exchanges, 1..4294967295 (default: 100)\n"
              << "  --attempts <number>  only 1 is allowed; retransmissions are disabled\n"
              << "  --output <path>      file for the received I/Q signal (default: rx.iq)\n"
              << "  -v, --verbose        enable trace logging\n"
              << "  -h, --help           show this help\n";
}

std::optional<CommandLineOptions> parseCommandLine(std::span<char*> args)
{
    CommandLineOptions options;
    for (size_t i = 1; i < args.size(); ++i)
    {
        const std::string_view arg = args[i];
        const bool hasValue = i + 1 < args.size();

        if (arg == "--exchange")
        {
            options.isExchange = true;
            continue;
        }
        if ((arg == "--count" || arg == "--attempts") && hasValue)
        {
            const auto value = parseSize(args[++i]);
            if (!value.has_value() || *value == 0 || *value > std::numeric_limits<uint32_t>::max() ||
                (arg == "--attempts" && *value != 1))
            {
                std::cerr << "Invalid number for " << arg << ": " << args[i] << '\n';
                return std::nullopt;
            }
            (arg == "--count" ? options.exchangeCount : options.maxAttempts) = *value;
            continue;
        }

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
        if (arg == "--mcs" && hasValue)
        {
            const auto value = parseSize(args[++i]);
            if (!value.has_value())
            {
                std::cerr << "Invalid MCS index: " << args[i] << '\n';
                return std::nullopt;
            }
            options.mcsIndex = *value;
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

    SignalManipulation::PhyConfig phyConfig;
    phyConfig.mcsIndex = options.mcsIndex;
    const auto phy = SignalManipulation::createPhyLayer(phyConfig, logger);
    if (phy == nullptr)
    {
        return EXIT_FAILURE;
    }

    if (options.isExchange)
    {
        Link::LinkConfig config;
        config.role = *options.role == Role::BaseStation ? Link::Role::BaseStation : Link::Role::Subscriber;
        config.exchangeCount = options.exchangeCount;
        config.maxAttempts = options.maxAttempts;
        return Link::runExchange(*device, *phy, config, *logger) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    return *options.role == Role::BaseStation ? runBaseStation(*device, *phy, *logger)
                                              : runSubscriber(*device, *phy, options, logger);
}

} // namespace Radiocom::Launcher
