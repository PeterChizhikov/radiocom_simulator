#include "SignalManipulation.h"

#include "ChannelCoding.h"
#include "Ofdm.h"

#include <cmath>
#include <format>
#include <numbers>
#include <utility>
#include <vector>


namespace Radiocom::SignalManipulation
{
namespace
{

constexpr size_t offsetEstimationPasses = 1;
constexpr double searchRangeHz = 15'000.0;
constexpr double hypothesisStepHz = 3'000.0;

class PhyLayer final : public IPhyLayer
{
public:
    PhyLayer(std::unique_ptr<OfdmModem> modem,
             std::unique_ptr<TurboCodec> turbo,
             std::shared_ptr<Logging::ILogger> logger,
             double sampleRateHz)
        : m_modem(std::move(modem))
        , m_turbo(std::move(turbo))
        , m_logger(std::move(logger))
        , m_sampleRateHz(sampleRateHz)
    {
        // Образец для поиска слота: символ с PSS, остальные поднесущие пусты
        ResourceGrid_t syncGrid{};
        mapSyncSignal(syncGrid);
        m_syncSymbol = m_modem->Modulate(syncGrid[syncSymbol], cyclicPrefixLength(syncSymbol));

        size_t dataElementCount = 0;
        for (size_t symbol = 0; symbol < symbolsPerSlot; ++symbol)
        {
            for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
            {
                dataElementCount += isDataElement(symbol, subcarrier) ? 1 : 0;
            }
        }

        // E = N_L * Q_m * G' при одном кодовом блоке, одном слое и QPSK (TS 36.212 V15.2.1, 5.1.4.1.2): все биты слота
        const size_t outputLength = dataElementCount * bitsPerSymbol;
        m_rateMatchingMap = buildRateMatchingMap(m_turbo->GetStreamLength(), outputLength);
        m_scrambling = generateGoldSequence(scramblingInit, outputLength);
    }

    size_t GetPayloadSize() const override { return payloadBytes; }

    std::optional<SyncPeak> FindSync(std::span<const Sample_t> signal) override
    {
        if (signal.size() < slotLength)
        {
            m_logger->Error(std::format("Signal of {} samples is shorter than a slot ({} samples)", signal.size(),
                                        slotLength));
            return std::nullopt;
        }

        // Сдвиг частоты смещает пик корреляции по времени, поэтому сначала перебираем гипотезы о сдвиге
        SyncPeak peak;
        double offsetHz = 0.0;
        for (double hypothesisHz = -searchRangeHz; hypothesisHz <= searchRangeHz; hypothesisHz += hypothesisStepHz)
        {
            const SyncPeak candidate = findCorrelationPeak(removeFrequencyOffset(signal, hypothesisHz));
            m_logger->Trace(std::format("Hypothesis {:.0f} Hz: slot at sample {}, correlation {:.3f}", hypothesisHz,
                                        candidate.position, candidate.correlation));
            if (candidate.correlation > peak.correlation)
            {
                peak = candidate;
                offsetHz = hypothesisHz;
            }
        }

        // У лучшей гипотезы остаточный сдвиг мал, и разность фаз половин оценивает его точно
        for (size_t pass = 0; pass < offsetEstimationPasses; ++pass)
        {
            offsetHz += estimateFrequencyOffset(removeFrequencyOffset(signal, offsetHz), peak.position);
            peak = findCorrelationPeak(removeFrequencyOffset(signal, offsetHz));
            m_logger->Trace(std::format("Pass {}: offset {:.0f} Hz, slot at sample {}, correlation {:.3f}", pass,
                                        offsetHz, peak.position, peak.correlation));
        }
        peak.frequencyOffsetHz = offsetHz;
        return peak;
    }

    std::optional<Signal_t> Modulate(std::span<const uint8_t> payload) override
    {
        if (payload.size() != payloadBytes)
        {
            m_logger->Error(std::format("Payload has to be {} bytes, got {}", payloadBytes, payload.size()));
            return std::nullopt;
        }

        const Bits_t block = attachCrc(bytesToBits(payload));
        const Bits_t streams = m_turbo->Encode(block);
        Bits_t codedBits(m_rateMatchingMap.size());
        for (size_t index = 0; index < codedBits.size(); ++index)
        {
            codedBits[index] = streams[m_rateMatchingMap[index]] ^ m_scrambling[index];
        }
        const Signal_t symbols = mapBitsToQpsk(codedBits);

        ResourceGrid_t grid{};
        mapSyncSignal(grid);
        mapReferenceSignals(grid);
        size_t nextSymbol = 0;
        for (size_t symbol = 0; symbol < symbolsPerSlot; ++symbol)
        {
            for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
            {
                if (isDataElement(symbol, subcarrier))
                {
                    grid[symbol][subcarrier] = symbols[nextSymbol++];
                }
            }
        }

        Signal_t slot;
        slot.reserve(slotLength);
        for (size_t symbol = 0; symbol < symbolsPerSlot; ++symbol)
        {
            const Signal_t samples = m_modem->Modulate(grid[symbol], cyclicPrefixLength(symbol));
            slot.insert(slot.end(), samples.begin(), samples.end());
        }
        m_logger->Trace(std::format("{} payload bytes -> {} block bits -> {} coded bits -> {} samples", payload.size(),
                                    block.size(), codedBits.size(), slot.size()));
        return slot;
    }

    std::optional<ReceivedFrame> Demodulate(std::span<const Sample_t> frame, double frequencyOffsetHz) override
    {
        if (frame.size() < slotLength)
        {
            m_logger->Error(std::format("Received signal of {} samples is shorter than a slot ({} samples)",
                                        frame.size(), slotLength));
            return std::nullopt;
        }

        const Signal_t corrected = removeFrequencyOffset(frame.first(slotLength), frequencyOffsetHz);
        ResourceGrid_t received;
        for (size_t symbol = 0; symbol < symbolsPerSlot; ++symbol)
        {
            const auto usefulPart =
                std::span(corrected).subspan(symbolOffset(symbol) + cyclicPrefixLength(symbol), fftSize);
            received[symbol] = m_modem->Demodulate(usefulPart);
        }

        const ResourceGrid_t channel = estimateChannel(received);
        Signal_t dataSymbols;
        Signal_t dataChannel;
        for (size_t symbol = 0; symbol < symbolsPerSlot; ++symbol)
        {
            for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
            {
                if (isDataElement(symbol, subcarrier))
                {
                    dataSymbols.push_back(received[symbol][subcarrier]);
                    dataChannel.push_back(channel[symbol][subcarrier]);
                }
            }
        }

        std::vector<float> soft = demapSoft(dataSymbols, dataChannel);
        Bytes_t codedBits(soft.size());
        for (size_t index = 0; index < soft.size(); ++index)
        {
            codedBits[index] = soft[index] < 0.0F ? 1 : 0;
            if (m_scrambling[index] != 0)
            {
                soft[index] = -soft[index];
            }
        }

        // Повторно переданные биты (E больше числа бит буфера) складываются
        std::vector<float> streams(turboStreamCount * m_turbo->GetStreamLength(), 0.0F);
        for (size_t index = 0; index < soft.size(); ++index)
        {
            streams[m_rateMatchingMap[index]] += soft[index];
        }

        std::optional<Bytes_t> payload;
        const auto block = m_turbo->Decode(streams);
        if (block.has_value() && calculateCrc(*block) == 0)
        {
            payload = bitsToBytes(std::span(*block).first(codeBlockSize - crcLength));
        }
        m_logger->Trace(std::format("{} coded bits -> CRC ok: {}", codedBits.size(), payload.has_value()));
        return ReceivedFrame{
            .codedBits = std::move(codedBits),
            .payload = std::move(payload),
        };
    }

private:
    Signal_t removeFrequencyOffset(std::span<const Sample_t> signal, double offsetHz) const
    {
        Signal_t corrected(signal.size());
        for (size_t index = 0; index < signal.size(); ++index)
        {
            const double phase = -2.0 * std::numbers::pi * offsetHz * double(index) / m_sampleRateHz;
            corrected[index] = signal[index] * std::polar(1.0, phase);
        }
        return corrected;
    }

    // Нормированная корреляция символа с PSS в каждой позиции слота, берётся лучшая
    SyncPeak findCorrelationPeak(std::span<const Sample_t> signal) const
    {
        const size_t syncSize = m_syncSymbol.size();
        double syncEnergy = 0.0;
        for (const auto& sample : m_syncSymbol)
        {
            syncEnergy += std::norm(sample);
        }

        SyncPeak peak;
        // Слот целиком должен помещаться в сигнал
        for (size_t position = 0; position + slotLength <= signal.size(); ++position)
        {
            const size_t syncStart = position + symbolOffset(syncSymbol);
            Sample_t product;
            double energy = 0.0;
            for (size_t index = 0; index < syncSize; ++index)
            {
                product += std::conj(m_syncSymbol[index]) * signal[syncStart + index];
                energy += std::norm(signal[syncStart + index]);
            }
            // По неравенству Коши — Буняковского значение не больше 1
            const double correlation = energy > 0.0 ? std::sqrt(std::norm(product) / (syncEnergy * energy)) : 0.0;
            if (correlation > peak.correlation)
            {
                peak.position = position;
                peak.correlation = correlation;
            }
        }
        return peak;
    }

    // Фаза между корреляциями двух половин PSS растёт на 2*pi*f*T, T — половина длительности
    double estimateFrequencyOffset(std::span<const Sample_t> signal, size_t slotStart) const
    {
        const size_t halfSize = m_syncSymbol.size() / 2;
        const size_t syncStart = slotStart + symbolOffset(syncSymbol);
        Sample_t firstHalf;
        Sample_t secondHalf;
        for (size_t index = 0; index < halfSize; ++index)
        {
            firstHalf += std::conj(m_syncSymbol[index]) * signal[syncStart + index];
            secondHalf += std::conj(m_syncSymbol[halfSize + index]) * signal[syncStart + halfSize + index];
        }
        const double phase = std::arg(secondHalf * std::conj(firstHalf));
        return phase * m_sampleRateHz / (2.0 * std::numbers::pi * double(halfSize));
    }

    std::unique_ptr<OfdmModem> m_modem;
    std::unique_ptr<TurboCodec> m_turbo;
    std::shared_ptr<Logging::ILogger> m_logger;
    double m_sampleRateHz;
    Signal_t m_syncSymbol;
    std::vector<size_t> m_rateMatchingMap;
    std::vector<uint8_t> m_scrambling;
};

} // namespace

std::unique_ptr<IPhyLayer> createPhyLayer(const PhyConfig& config, std::shared_ptr<Logging::ILogger> logger)
{
    if (logger == nullptr)
    {
        return nullptr;
    }

    auto modem = OfdmModem::Create();
    if (modem == nullptr)
    {
        logger->Error(std::format("Cannot create PHY layer: FFTW planning of size {} failed", fftSize));
        return nullptr;
    }

    auto turbo = TurboCodec::Create(codeBlockSize);
    if (turbo == nullptr)
    {
        logger->Error(std::format("Cannot create PHY layer: turbo codec for block size {} failed", codeBlockSize));
        return nullptr;
    }

    return std::make_unique<PhyLayer>(std::move(modem), std::move(turbo), std::move(logger), config.sampleRateHz);
}

} // namespace Radiocom::SignalManipulation
