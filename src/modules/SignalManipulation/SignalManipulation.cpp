#include "SignalManipulation.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <format>
#include <random>
#include <string>
#include <utility>

#include <fftw3.h>

// Hamming.h — сторонний код, его предупреждения отключаем, а не правим
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "Hamming.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif


namespace Radiocom::SignalManipulation
{
namespace
{

constexpr size_t bitsPerByte = 8;
constexpr size_t bitsPerSymbol = 2;
constexpr size_t symbolsPerByte = bitsPerByte / bitsPerSymbol;
constexpr uint8_t dibitMask = 0b11;
constexpr double qpskAmplitude = 0.707;

constexpr std::array<Sample_t, 4> qpskConstellation = {
    Sample_t(qpskAmplitude, qpskAmplitude),
    Sample_t(-qpskAmplitude, qpskAmplitude),
    Sample_t(qpskAmplitude, -qpskAmplitude),
    Sample_t(-qpskAmplitude, -qpskAmplitude),
};

using HammingCodec_t = Hamming4;

Bytes_t encodeHamming(std::span<const uint8_t> payload)
{
    Bytes_t encoded(HammingCodec_t::encodedSize(payload.size()), 0);
    HammingCodec_t::encode(encoded.data(), payload.data(), payload.size());
    return encoded;
}

std::optional<Bytes_t> decodeHamming(Bytes_t encoded)
{
    const size_t decodedSize = HammingCodec_t::decodedSize(encoded.size());
    if (decodedSize == 0)
    {
        return std::nullopt;
    }

    if (!HammingCodec_t::decode(encoded.data(), encoded.size()))
    {
        return std::nullopt;
    }
    encoded.resize(decodedSize);
    return encoded;
}

Signal_t mapToQpsk(std::span<const uint8_t> bytes)
{
    Signal_t symbols;
    symbols.reserve(bytes.size() * symbolsPerByte);
    for (const uint8_t byte : bytes)
    {
        for (size_t shift = 0; shift < bitsPerByte; shift += bitsPerSymbol)
        {
            symbols.push_back(qpskConstellation[(byte >> shift) & dibitMask]);
        }
    }
    return symbols;
}

uint8_t demapQpskSymbol(const Sample_t& symbol)
{
    const uint8_t inPhaseBit = symbol.real() < 0.0 ? 1 : 0;
    const uint8_t quadratureBit = symbol.imag() < 0.0 ? 1 : 0;
    return uint8_t(inPhaseBit | (quadratureBit << 1));
}

Bytes_t demapFromQpsk(std::span<const Sample_t> symbols)
{
    Bytes_t bytes;
    bytes.reserve(symbols.size() / symbolsPerByte);

    uint8_t currentByte = 0;
    size_t shift = 0;
    for (const auto& symbol : symbols)
    {
        currentByte |= uint8_t(demapQpskSymbol(symbol) << shift);
        shift += bitsPerSymbol;
        if (shift == bitsPerByte)
        {
            bytes.push_back(currentByte);
            currentByte = 0;
            shift = 0;
        }
    }
    return bytes;
}

class FftwPlan
{
public:
    FftwPlan() = default;
    explicit FftwPlan(fftw_plan plan) noexcept
        : m_plan(plan)
    {
    }
    ~FftwPlan()
    {
        if (m_plan != nullptr)
        {
            fftw_destroy_plan(m_plan);
        }
    }

    FftwPlan(const FftwPlan& other) = delete;
    FftwPlan(FftwPlan&& other) noexcept
        : m_plan(std::exchange(other.m_plan, nullptr))
    {
    }
    FftwPlan& operator=(const FftwPlan& other) = delete;
    FftwPlan& operator=(FftwPlan&& other) noexcept
    {
        std::swap(m_plan, other.m_plan);
        return *this;
    }

    bool IsValid() const { return m_plan != nullptr; }
    void Execute() const { fftw_execute(m_plan); }

private:
    fftw_plan m_plan = nullptr;
};

class OfdmModem
{
public:
    static std::unique_ptr<OfdmModem> Create(const PipelineConfig& config)
    {
        auto modem = std::unique_ptr<OfdmModem>(new OfdmModem(config));

        const int fftSize = int(config.fftSize);
        auto* frequencyData = reinterpret_cast<fftw_complex*>(modem->m_frequencyBuffer.data());
        auto* timeData = reinterpret_cast<fftw_complex*>(modem->m_timeBuffer.data());
        modem->m_inversePlan =
            FftwPlan(fftw_plan_dft_1d(fftSize, frequencyData, timeData, FFTW_BACKWARD, FFTW_ESTIMATE));
        modem->m_forwardPlan =
            FftwPlan(fftw_plan_dft_1d(fftSize, timeData, frequencyData, FFTW_FORWARD, FFTW_ESTIMATE));

        if (!modem->m_inversePlan.IsValid() || !modem->m_forwardPlan.IsValid())
        {
            return nullptr;
        }
        return modem;
    }

    OfdmModem(const OfdmModem& other) = delete;
    OfdmModem(OfdmModem&& other) = delete;
    OfdmModem& operator=(const OfdmModem& other) = delete;
    OfdmModem& operator=(OfdmModem&& other) = delete;
    ~OfdmModem() = default;

    size_t GetSymbolCapacity() const { return 2 * m_subcarriersPerSide; }
    size_t GetFrameLength() const { return m_cyclicPrefixSize + m_fftSize; }

    Signal_t Modulate(std::span<const Sample_t> symbols)
    {
        std::fill(m_frequencyBuffer.begin(), m_frequencyBuffer.end(), Sample_t());
        const size_t halfCount = symbols.size() / 2;
        const auto firstHalf = symbols.first(halfCount);
        const auto secondHalf = symbols.subspan(halfCount, halfCount);
        std::copy(firstHalf.begin(), firstHalf.end(), m_frequencyBuffer.begin() + 1);
        std::copy(secondHalf.begin(), secondHalf.end(), m_frequencyBuffer.end() - std::ptrdiff_t(halfCount));

        m_inversePlan.Execute();
        // FFTW не нормирует обратное преобразование
        for (auto& sample : m_timeBuffer)
        {
            sample /= double(m_fftSize);
        }

        Signal_t frame;
        frame.reserve(GetFrameLength());
        frame.insert(frame.end(), m_timeBuffer.end() - std::ptrdiff_t(m_cyclicPrefixSize), m_timeBuffer.end());
        frame.insert(frame.end(), m_timeBuffer.begin(), m_timeBuffer.end());
        return frame;
    }

    Signal_t Demodulate(std::span<const Sample_t> frame, size_t symbolCount)
    {
        const auto usefulPart = frame.subspan(m_cyclicPrefixSize, m_fftSize);
        std::copy(usefulPart.begin(), usefulPart.end(), m_timeBuffer.begin());
        m_forwardPlan.Execute();

        const auto halfCount = std::ptrdiff_t(symbolCount / 2);
        const auto firstHalfBegin = m_frequencyBuffer.begin() + 1;
        Signal_t symbols;
        symbols.reserve(2 * size_t(halfCount));
        symbols.insert(symbols.end(), firstHalfBegin, firstHalfBegin + halfCount);
        symbols.insert(symbols.end(), m_frequencyBuffer.end() - halfCount, m_frequencyBuffer.end());
        return symbols;
    }

private:
    explicit OfdmModem(const PipelineConfig& config)
        : m_fftSize(config.fftSize)
        , m_cyclicPrefixSize(config.cyclicPrefixSize)
        , m_subcarriersPerSide(config.subcarriersPerSide)
        , m_frequencyBuffer(config.fftSize)
        , m_timeBuffer(config.fftSize)
    {
    }

    size_t m_fftSize = 0;
    size_t m_cyclicPrefixSize = 0;
    size_t m_subcarriersPerSide = 0;
    Signal_t m_frequencyBuffer;
    Signal_t m_timeBuffer;
    // Планы FFTW привязаны к адресам буферов: объявлены после них, чтобы разрушаться первыми
    FftwPlan m_inversePlan;
    FftwPlan m_forwardPlan;
};

std::optional<std::string> validateConfig(const PipelineConfig& config)
{
    if (config.fftSize == 0 || config.fftSize > size_t(INT_MAX))
    {
        return std::format("FFT size {} is out of range", config.fftSize);
    }
    if (config.cyclicPrefixSize > config.fftSize)
    {
        return std::format("cyclic prefix of {} samples exceeds FFT size {}", config.cyclicPrefixSize,
                           config.fftSize);
    }
    if (2 * config.subcarriersPerSide + 1 > config.fftSize)
    {
        return std::format("{} subcarriers per side do not fit into FFT size {}", config.subcarriersPerSide,
                           config.fftSize);
    }
    return std::nullopt;
}

Signal_t addAwgn(std::span<const Sample_t> signal, double snrDb, std::mt19937& generator)
{
    Signal_t noisySignal(signal.begin(), signal.end());
    if (signal.empty())
    {
        return noisySignal;
    }

    double signalPower = 0.0;
    for (const auto& sample : signal)
    {
        signalPower += std::norm(sample);
    }
    signalPower /= double(signal.size());

    const double snrLinear = std::pow(10.0, snrDb / 10.0);
    const double noisePower = signalPower / snrLinear;
    const double noiseStdDev = std::sqrt(noisePower / 2.0);

    std::normal_distribution<double> distribution(0.0, noiseStdDev);
    for (auto& sample : noisySignal)
    {
        const double inPhaseNoise = distribution(generator);
        const double quadratureNoise = distribution(generator);
        sample += Sample_t(inPhaseNoise, quadratureNoise);
    }
    return noisySignal;
}

std::optional<double> calculateBitErrorRate(std::span<const uint8_t> reference, std::span<const uint8_t> received)
{
    if (reference.empty() || reference.size() != received.size())
    {
        return std::nullopt;
    }

    size_t errorCount = 0;
    for (size_t i = 0; i < reference.size(); ++i)
    {
        errorCount += size_t(std::popcount(uint8_t(reference[i] ^ received[i])));
    }
    return double(errorCount) / double(reference.size() * bitsPerByte);
}

class TransmissionPipeline final : public ITransmissionPipeline
{
public:
    explicit TransmissionPipeline(std::unique_ptr<OfdmModem> modem,
                                  std::shared_ptr<SignalFileIO::ISignalStorage> storage,
                                  std::shared_ptr<Logging::ILogger> logger,
                                  uint32_t seed)
        : m_modem(std::move(modem))
        , m_storage(std::move(storage))
        , m_logger(std::move(logger))
        , m_generator(seed)
    {
    }

    std::optional<TransmissionReport> Run(std::span<const uint8_t> payload, double snrDb) override
    {
        const Bytes_t encoded = encodeHamming(payload);
        const Signal_t symbols = mapToQpsk(encoded);
        if (symbols.size() > m_modem->GetSymbolCapacity())
        {
            m_logger->Error(std::format("Payload of {} bytes needs {} QPSK symbols, OFDM symbol capacity is {}",
                                        payload.size(), symbols.size(), m_modem->GetSymbolCapacity()));
            return std::nullopt;
        }

        const Signal_t frame = m_modem->Modulate(symbols);
        m_logger->Trace(std::format("SNR {} dB: {} payload bytes -> {} encoded bytes -> {} QPSK symbols -> {} samples",
                                    snrDb, payload.size(), encoded.size(), symbols.size(), frame.size()));

        const Signal_t noisyFrame = addAwgn(frame, snrDb, m_generator);
        if (!m_storage->Save(noisyFrame))
        {
            m_logger->Error(std::format("Transmission at SNR {} dB aborted: failed to store the signal", snrDb));
            return std::nullopt;
        }

        const auto receivedFrame = m_storage->Load();
        if (!receivedFrame.has_value())
        {
            m_logger->Error(std::format("Transmission at SNR {} dB aborted: failed to load the signal", snrDb));
            return std::nullopt;
        }
        if (receivedFrame->size() < m_modem->GetFrameLength())
        {
            m_logger->Error(std::format("Received signal of {} samples is shorter than one OFDM symbol ({} samples)",
                                        receivedFrame->size(), m_modem->GetFrameLength()));
            return std::nullopt;
        }

        const Signal_t receivedSymbols = m_modem->Demodulate(*receivedFrame, symbols.size());
        Bytes_t received = demapFromQpsk(receivedSymbols);

        const auto bitErrorRate = calculateBitErrorRate(encoded, received);
        if (!bitErrorRate.has_value())
        {
            m_logger->Error(std::format("Transmission at SNR {} dB aborted: sent {} bytes but received {} bytes", snrDb,
                                        encoded.size(), received.size()));
            return std::nullopt;
        }

        auto decoded = decodeHamming(std::move(received));
        const bool isDecoded = decoded.has_value();
        m_logger->Trace(std::format("SNR {} dB: BER {}, decoded: {}", snrDb, *bitErrorRate, isDecoded));

        return TransmissionReport{
            .snrDb = snrDb,
            .bitErrorRate = *bitErrorRate,
            .isDecoded = isDecoded,
            .decodedPayload = std::move(decoded).value_or(Bytes_t()),
        };
    }

private:
    std::unique_ptr<OfdmModem> m_modem;
    std::shared_ptr<SignalFileIO::ISignalStorage> m_storage;
    std::shared_ptr<Logging::ILogger> m_logger;
    std::mt19937 m_generator;
};

} // namespace

std::unique_ptr<ITransmissionPipeline> createTransmissionPipeline(const PipelineConfig& config,
                                                                  std::shared_ptr<SignalFileIO::ISignalStorage> storage,
                                                                  std::shared_ptr<Logging::ILogger> logger)
{
    if (logger == nullptr)
    {
        return nullptr;
    }
    if (storage == nullptr)
    {
        logger->Error("Cannot create transmission pipeline: signal storage is not provided");
        return nullptr;
    }
    if (const auto error = validateConfig(config); error.has_value())
    {
        logger->Error("Cannot create transmission pipeline: " + *error);
        return nullptr;
    }

    auto modem = OfdmModem::Create(config);
    if (modem == nullptr)
    {
        logger->Error(std::format("Cannot create transmission pipeline: FFTW planning of size {} failed",
                                  config.fftSize));
        return nullptr;
    }

    const uint32_t seed = config.channelSeed.value_or(std::random_device()());
    logger->Debug(std::format("AWGN channel seed: {}", seed));

    return std::make_unique<TransmissionPipeline>(std::move(modem), std::move(storage), std::move(logger), seed);
}

} // namespace Radiocom::SignalManipulation
