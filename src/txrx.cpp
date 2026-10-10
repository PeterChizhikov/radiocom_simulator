#include "Logger.h"
#include "Sdr.h"
#include "SignalFileIO.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fftw3.h>


namespace
{

namespace Logging = Radiocom::Logging;
namespace Sdr = Radiocom::Sdr;
namespace SignalFileIO = Radiocom::SignalFileIO;

// 3840 отсчётов при 3.84 MSPS — ровно 100 периодов тона, поэтому циклический буфер повторяется без разрыва фазы
constexpr size_t toneBufferSize = 3840;
constexpr double toneOffsetHz = 100'000.0;
constexpr double toneAmplitude = 0.5;
constexpr size_t receiveSize = 65536;
// Вблизи нуля частоты лежат постоянная составляющая и утечка гетеродина, пик там не ищем
constexpr double dcExclusionHz = 10'000.0;

struct CommandLineOptions
{
    bool isHelpRequested = false;
    bool isVerbose = false;
    bool isTransmitEnabled = true;
    std::string txUri;
    std::string rxUri;
    double txAttenuationDb = 89.75;
    double rxGainDb = 0.0;
    std::string outputPath = "rx.iq";
};

void printUsage(std::string_view programName)
{
    std::cout << "Usage: " << programName << " --tx <uri> [options]\n"
              << "  --tx <uri>           transmitting Pluto, e.g. usb:1.2.5\n"
              << "  --rx <uri>           receiving Pluto (default: same as --tx)\n"
              << "  --tx-atten <db>      TX attenuation, 40..89.75 dB (default: 89.75)\n"
              << "  --rx-gain <db>       manual RX gain, -3..71 dB (default: 0)\n"
              << "  --no-tx              receive only, to measure the noise floor\n"
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
        if (arg == "--no-tx")
        {
            options.isTransmitEnabled = false;
            continue;
        }
        if (arg == "--tx" && hasValue)
        {
            options.txUri = args[++i];
            continue;
        }
        if (arg == "--rx" && hasValue)
        {
            options.rxUri = args[++i];
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

    if (options.txUri.empty() && !options.isHelpRequested)
    {
        std::cerr << "--tx is required\n";
        return std::nullopt;
    }
    if (options.rxUri.empty())
    {
        options.rxUri = options.txUri;
    }
    return options;
}

Radiocom::Signal_t generateTone(double sampleRateHz)
{
    Radiocom::Signal_t tone(toneBufferSize);
    for (size_t n = 0; n < tone.size(); ++n)
    {
        const double phase = 2.0 * std::numbers::pi * toneOffsetHz * double(n) / sampleRateHz;
        tone[n] = std::polar(toneAmplitude, phase);
    }
    return tone;
}

// Мощность спектра с окном Ханна, бины в порядке FFTW: 0..N/2-1 — положительные частоты, далее отрицательные
std::vector<double> computePowerSpectrum(const Radiocom::Signal_t& signal)
{
    const size_t size = signal.size();
    Radiocom::Signal_t spectrum(size);
    for (size_t n = 0; n < size; ++n)
    {
        const double window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * double(n) / double(size));
        spectrum[n] = signal[n] * window;
    }

    auto* data = reinterpret_cast<fftw_complex*>(spectrum.data());
    const fftw_plan plan = fftw_plan_dft_1d(int(size), data, data, FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_execute(plan);
    fftw_destroy_plan(plan);

    std::vector<double> power(size);
    std::ranges::transform(spectrum, power.begin(), [](const Radiocom::Sample_t& bin) { return std::norm(bin); });
    return power;
}

double toDb(double power)
{
    return 10.0 * std::log10(power);
}

void reportSignal(const Radiocom::Signal_t& signal, double sampleRateHz, Logging::ILogger& logger)
{
    double meanPower = 0.0;
    double maxComponent = 0.0;
    for (const auto& sample : signal)
    {
        meanPower += std::norm(sample);
        maxComponent = std::max({maxComponent, std::abs(sample.real()), std::abs(sample.imag())});
    }
    meanPower /= double(signal.size());
    logger.Info(std::format("RX level: {:.1f} dBFS, max |I|,|Q| = {:.3f} of full scale{}", toDb(meanPower),
                            maxComponent, maxComponent >= 0.999 ? " — CLIPPING, lower the RX gain" : ""));

    const auto power = computePowerSpectrum(signal);
    const double binHz = sampleRateHz / double(power.size());
    const auto toFrequency = [&](size_t bin)
    { return bin < power.size() / 2 ? double(bin) * binHz : (double(bin) - double(power.size())) * binHz; };

    // Поиск начинается с бина ожидаемого тона, а не с нулевого, где постоянная составляющая
    size_t peakBin = size_t(std::lround(toneOffsetHz / binHz));
    for (size_t bin = 0; bin < power.size(); ++bin)
    {
        if (std::abs(toFrequency(bin)) > dcExclusionHz && power[bin] > power[peakBin])
        {
            peakBin = bin;
        }
    }

    auto sorted = power;
    std::ranges::nth_element(sorted, sorted.begin() + std::ptrdiff_t(sorted.size() / 2));
    const double noiseFloor = sorted[sorted.size() / 2];

    // Сдвиг пика от ожидаемой частоты — расхождение опорных генераторов передатчика и приёмника
    const double peakHz = toFrequency(peakBin);
    logger.Info(std::format("Peak: {:.0f} Hz (expected {:.0f} Hz, offset {:.0f} Hz, resolution {:.0f} Hz), "
                            "{:.1f} dB above the median noise floor",
                            peakHz, toneOffsetHz, peakHz - toneOffsetHz, binHz, toDb(power[peakBin] / noiseFloor)));
}

int runTxRx(const CommandLineOptions& options)
{
    const auto logger =
        Logging::createConsoleLogger(options.isVerbose ? Logging::LogLevel::Trace : Logging::LogLevel::Info);
    if (logger == nullptr)
    {
        return EXIT_FAILURE;
    }

    Sdr::PlutoConfig txConfig;
    txConfig.uri = options.txUri;
    txConfig.txAttenuationDb = options.txAttenuationDb;
    txConfig.rxGainDb = options.rxGainDb;
    const auto txDevice = Sdr::PlutoDevice::Create(txConfig, logger);
    if (txDevice == nullptr)
    {
        return EXIT_FAILURE;
    }

    // Если приёмник — другой Pluto, его передатчик остаётся на максимальном ослаблении
    std::unique_ptr<Sdr::PlutoDevice> separateRxDevice;
    if (options.rxUri != options.txUri)
    {
        Sdr::PlutoConfig rxConfig = txConfig;
        rxConfig.uri = options.rxUri;
        rxConfig.txAttenuationDb = Sdr::PlutoConfig().txAttenuationDb;
        separateRxDevice = Sdr::PlutoDevice::Create(rxConfig, logger);
        if (separateRxDevice == nullptr)
        {
            return EXIT_FAILURE;
        }
    }
    Sdr::PlutoDevice& rxDevice = separateRxDevice != nullptr ? *separateRxDevice : *txDevice;

    const double sampleRateHz = double(txConfig.sampleRateHz);
    if (options.isTransmitEnabled && !txDevice->StartTransmit(generateTone(sampleRateHz)))
    {
        return EXIT_FAILURE;
    }

    const auto received = rxDevice.Receive(receiveSize);
    txDevice->StopTransmit();
    if (!received.has_value())
    {
        return EXIT_FAILURE;
    }

    reportSignal(*received, sampleRateHz, *logger);
    SignalFileIO::BinaryFileSignalStorage storage(options.outputPath, logger);
    return storage.Save(*received) ? EXIT_SUCCESS : EXIT_FAILURE;
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
        return runTxRx(*options);
    }
    catch (const std::exception& e)
    {
        std::cerr << "TX/RX test terminated by an unhandled exception: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
