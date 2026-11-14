#include "SignalManipulation.h"

#include "ChannelCoding.h"
#include "Frame.h"
#include "Ofdm.h"

#include <algorithm>
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

// Расстояние от начала символа SSS до начала символа PSS той же половины кадра
constexpr size_t sssToPssDistance = frameSymbolOffset(pssSubframes[0], pssSymbol) - frameSymbolOffset(0, sssSymbol);

// Согласование скорости и скремблирование одного субкадра с данными
struct SubframeCoding
{
    size_t subframe = 0;
    // Карта согласования скорости каждого кодового блока, длины E_r
    std::vector<std::vector<size_t>> rateMatchingMaps;
    // По одному биту на каждый из G бит субкадра
    std::vector<uint8_t> scrambling;
};

class PhyLayer final : public IPhyLayer
{
public:
    PhyLayer(std::unique_ptr<OfdmModem> modem,
             std::unique_ptr<TurboCodec> turbo,
             std::shared_ptr<Logging::ILogger> logger,
             double sampleRateHz,
             ModulationAndCoding modulationAndCoding,
             Segmentation segmentation)
        : m_modem(std::move(modem))
        , m_turbo(std::move(turbo))
        , m_logger(std::move(logger))
        , m_sampleRateHz(sampleRateHz)
        , m_modulationAndCoding(modulationAndCoding)
        , m_segmentation(segmentation)
        , m_pss(generatePss())
    {
        // Образец для поиска кадра: символ с PSS, остальные поднесущие пусты
        ResourceGrid_t syncGrid{};
        std::ranges::copy(m_pss, syncGrid[pssSymbol].begin() + std::ptrdiff_t(syncFirstSubcarrier));
        m_pssSymbol = m_modem->Modulate(syncGrid[pssSymbol], cyclicPrefixLength(pssSymbol));

        for (size_t half = 0; half < sssSubframes.size(); ++half)
        {
            m_sss[half] = generateSss(sssSubframes[half]);
        }

        // G — все биты элементов PDSCH субкадра, делятся между кодовыми блоками (TS 36.212 V15.2.1, 5.1.4.1.2)
        const size_t modulationOrder = m_modulationAndCoding.modulationOrder;
        for (const size_t subframe : dataSubframes)
        {
            const size_t availableBits = countDataElements(subframe) * modulationOrder;
            SubframeCoding coding{
                .subframe = subframe,
                .rateMatchingMaps = {},
                .scrambling = generateGoldSequence(scramblingInit(subframe), availableBits),
            };
            for (const size_t outputLength :
                 rateMatchingOutputLengths(availableBits, modulationOrder, m_segmentation.blockCount))
            {
                coding.rateMatchingMaps.push_back(buildRateMatchingMap(m_turbo->GetStreamLength(), outputLength));
            }
            m_coding.push_back(std::move(coding));
        }
    }

    size_t GetPayloadSize() const override { return m_modulationAndCoding.transportBlockSize / bitsPerByte; }

    size_t GetFrameLength() const override { return frameLength; }

    std::span<const size_t> GetDataSubframes() const override { return dataSubframes; }

    std::optional<SyncPeak> FindSync(std::span<const Sample_t> signal) override
    {
        if (signal.size() < frameLength + sssToPssDistance)
        {
            m_logger->Error(std::format("Signal of {} samples is shorter than a frame ({} samples)", signal.size(),
                                        frameLength + sssToPssDistance));
            return std::nullopt;
        }

        // Сдвиг частоты смещает пик корреляции по времени, поэтому сначала перебираем гипотезы о сдвиге
        PssPeak peak;
        double offsetHz = 0.0;
        for (double hypothesisHz = -searchRangeHz; hypothesisHz <= searchRangeHz; hypothesisHz += hypothesisStepHz)
        {
            const PssPeak candidate = findPssPeak(removeFrequencyOffset(signal, hypothesisHz));
            m_logger->Trace(std::format("Hypothesis {:.0f} Hz: PSS at sample {}, correlation {:.3f}", hypothesisHz,
                                        candidate.position, candidate.correlation));
            if (candidate.correlation > peak.correlation)
            {
                peak = candidate;
                offsetHz = hypothesisHz;
            }
        }

        // У лучшей гипотезы остаточный сдвиг мал, и разность фаз половин оценивает его точно
        Signal_t corrected;
        for (size_t pass = 0; pass < offsetEstimationPasses; ++pass)
        {
            offsetHz += estimateFrequencyOffset(removeFrequencyOffset(signal, offsetHz), peak.position);
            corrected = removeFrequencyOffset(signal, offsetHz);
            peak = findPssPeak(corrected);
            m_logger->Trace(std::format("Pass {}: offset {:.0f} Hz, PSS at sample {}, correlation {:.3f}", pass,
                                        offsetHz, peak.position, peak.correlation));
        }

        const size_t half = detectHalfFrame(corrected, peak.position);
        const size_t pssOffset = frameSymbolOffset(pssSubframes[half], pssSymbol);
        const size_t frameStart = (peak.position + frameLength - pssOffset) % frameLength;
        if (frameStart + frameLength > signal.size())
        {
            m_logger->Error(std::format("Frame starting at sample {} does not fit into {} samples", frameStart,
                                        signal.size()));
            return std::nullopt;
        }
        return SyncPeak{
            .position = frameStart,
            .correlation = peak.correlation,
            .frequencyOffsetHz = offsetHz,
        };
    }

    std::optional<Signal_t> Modulate(std::span<const Bytes_t> payloads) override
    {
        if (payloads.size() != dataSubframes.size())
        {
            m_logger->Error(std::format("Frame needs {} payloads, got {}", dataSubframes.size(), payloads.size()));
            return std::nullopt;
        }
        for (const auto& payload : payloads)
        {
            if (payload.size() != GetPayloadSize())
            {
                m_logger->Error(std::format("Payload has to be {} bytes, got {}", GetPayloadSize(), payload.size()));
                return std::nullopt;
            }
        }

        Signal_t frame;
        frame.reserve(frameLength);
        for (size_t subframe = 0; subframe < subframesPerFrame; ++subframe)
        {
            ResourceGrid_t grid{};
            mapSignals(grid, subframe);
            const auto coding = std::ranges::find(m_coding, subframe, &SubframeCoding::subframe);
            if (coding != m_coding.end())
            {
                const Signal_t symbols = encode(payloads[size_t(coding - m_coding.begin())], *coding);
                placeData(grid, subframe, symbols);
            }
            const Signal_t samples = m_modem->ModulateSubframe(grid);
            frame.insert(frame.end(), samples.begin(), samples.end());
        }
        m_logger->Trace(std::format("{} payloads of {} bytes -> frame of {} samples", payloads.size(),
                                    GetPayloadSize(), frame.size()));
        return frame;
    }

    std::optional<std::vector<ReceivedSubframe>> Demodulate(std::span<const Sample_t> frame,
                                                            double frequencyOffsetHz) override
    {
        if (frame.size() < frameLength)
        {
            m_logger->Error(std::format("Received signal of {} samples is shorter than a frame ({} samples)",
                                        frame.size(), frameLength));
            return std::nullopt;
        }

        const Signal_t corrected = removeFrequencyOffset(frame.first(frameLength), frequencyOffsetHz);
        std::vector<ReceivedSubframe> result;
        for (const auto& coding : m_coding)
        {
            const auto samples = std::span(corrected).subspan(coding.subframe * subframeLength, subframeLength);
            result.push_back(decode(m_modem->DemodulateSubframe(samples), coding));
        }
        return result;
    }

    std::optional<Bytes_t> DemodulateRepeatedPayload(std::span<const Sample_t> frame,
                                                    double frequencyOffsetHz) override
    {
        if (frame.size() < frameLength)
        {
            m_logger->Error(std::format("Repeated-payload frame needs {} samples, got {}", frameLength, frame.size()));
            return std::nullopt;
        }

        const Signal_t corrected = removeFrequencyOffset(frame.first(frameLength), frequencyOffsetHz);
        std::vector<std::vector<float>> combined(
            m_segmentation.blockCount, std::vector<float>(turboStreamCount * m_turbo->GetStreamLength(), 0.0F));
        for (const auto& coding : m_coding)
        {
            const auto samples = std::span(corrected).subspan(coding.subframe * subframeLength, subframeLength);
            const auto streams = demapCodeBlocks(m_modem->DemodulateSubframe(samples), coding, nullptr);
            if (auto payload = decodeTransportBlock(streams); payload.has_value())
            {
                return payload;
            }
            for (size_t block = 0; block < combined.size(); ++block)
            {
                for (size_t bit = 0; bit < combined[block].size(); ++bit)
                {
                    combined[block][bit] += streams[block][bit];
                }
            }
        }
        auto payload = decodeTransportBlock(combined);
        if (payload.has_value())
        {
            m_logger->Debug("Payload decoded by combining the four subframe copies");
        }
        return payload;
    }

private:
    struct PssPeak
    {
        size_t position = 0;
        double correlation = 0.0;
    };

    // Сегментация и CRC24B (TS 36.212 V15.2.1, 5.1.2), турбокод и согласование скорости каждого блока, конкатенация
    // выходов блоков по порядку (5.1.5), скремблирование (TS 36.211 V16.2.0, 6.3.1), модуляция (7.1)
    Signal_t encode(std::span<const uint8_t> payload, const SubframeCoding& coding)
    {
        const Bits_t transportBlock = attachCrc(bytesToBits(payload));
        const size_t blockPayload = codeBlockPayloadSize(m_segmentation);
        Bits_t codedBits;
        codedBits.reserve(coding.scrambling.size());
        for (size_t block = 0; block < m_segmentation.blockCount; ++block)
        {
            const auto segment = std::span(transportBlock).subspan(block * blockPayload, blockPayload);
            const Bits_t codeBlock =
                m_segmentation.blockCount == 1 ? Bits_t(segment.begin(), segment.end())
                                               : attachCrc(segment, CrcType::CodeBlock);
            const Bits_t streams = m_turbo->Encode(codeBlock);
            for (const size_t position : coding.rateMatchingMaps[block])
            {
                codedBits.push_back(streams[position]);
            }
        }
        for (size_t index = 0; index < codedBits.size(); ++index)
        {
            codedBits[index] ^= coding.scrambling[index];
        }
        return mapBits(codedBits, m_modulationAndCoding.modulationOrder);
    }

    // PDSCH заполняет элементы сначала по поднесущим, затем по символам (TS 36.211 V16.2.0, 6.3.5)
    static void placeData(ResourceGrid_t& grid, size_t subframe, std::span<const Sample_t> symbols)
    {
        size_t next = 0;
        for (size_t symbol = 0; symbol < symbolsPerSubframe; ++symbol)
        {
            for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
            {
                if (isDataElement(subframe, symbol, subcarrier))
                {
                    grid[symbol][subcarrier] = symbols[next++];
                }
            }
        }
    }

    std::vector<std::vector<float>> demapCodeBlocks(const ResourceGrid_t& received,
                                                   const SubframeCoding& coding,
                                                   Bytes_t* codedBits)
    {
        const ResourceGrid_t channel = estimateChannel(received, coding.subframe);
        Signal_t dataSymbols;
        Signal_t dataChannel;
        for (size_t symbol = 0; symbol < symbolsPerSubframe; ++symbol)
        {
            for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
            {
                if (isDataElement(coding.subframe, symbol, subcarrier))
                {
                    dataSymbols.push_back(received[symbol][subcarrier]);
                    dataChannel.push_back(channel[symbol][subcarrier]);
                }
            }
        }

        std::vector<float> soft = demapSoft(dataSymbols, dataChannel, m_modulationAndCoding.modulationOrder);
        if (codedBits != nullptr)
        {
            codedBits->resize(soft.size());
        }
        for (size_t index = 0; index < soft.size(); ++index)
        {
            if (codedBits != nullptr)
            {
                (*codedBits)[index] = soft[index] < 0.0F ? 1 : 0;
            }
            if (coding.scrambling[index] != 0)
            {
                soft[index] = -soft[index];
            }
        }

        std::vector<std::vector<float>> codeBlocks;
        codeBlocks.reserve(m_segmentation.blockCount);
        size_t offset = 0;
        for (const auto& rateMatchingMap : coding.rateMatchingMaps)
        {
            // Повторно переданные биты (E больше числа бит буфера) складываются
            std::vector<float> streams(turboStreamCount * m_turbo->GetStreamLength(), 0.0F);
            for (size_t index = 0; index < rateMatchingMap.size(); ++index)
            {
                streams[rateMatchingMap[index]] += soft[offset + index];
            }
            offset += rateMatchingMap.size();
            codeBlocks.push_back(std::move(streams));
        }
        return codeBlocks;
    }

    std::optional<Bytes_t> decodeTransportBlock(std::span<const std::vector<float>> codeBlocks)
    {
        const size_t blockPayload = codeBlockPayloadSize(m_segmentation);
        Bits_t transportBlock;
        transportBlock.reserve(m_segmentation.blockCount * blockPayload);
        for (const auto& streams : codeBlocks)
        {
            const auto block = m_turbo->Decode(streams);
            if (!block.has_value() ||
                (m_segmentation.blockCount > 1 && calculateCrc(*block, CrcType::CodeBlock) != 0))
            {
                return std::nullopt;
            }
            transportBlock.insert(transportBlock.end(), block->begin(),
                                  block->begin() + std::ptrdiff_t(blockPayload));
        }

        if (calculateCrc(transportBlock) != 0)
        {
            return std::nullopt;
        }
        return bitsToBytes(std::span(transportBlock).first(m_modulationAndCoding.transportBlockSize));
    }

    ReceivedSubframe decode(const ResourceGrid_t& received, const SubframeCoding& coding)
    {
        Bytes_t codedBits;
        const auto streams = demapCodeBlocks(received, coding, &codedBits);
        auto payload = decodeTransportBlock(streams);
        m_logger->Trace(std::format("Subframe {}: {} coded bits, CRC ok: {}", coding.subframe, codedBits.size(),
                                    payload.has_value()));
        return ReceivedSubframe{
            .subframe = coding.subframe,
            .codedBits = std::move(codedBits),
            .payload = std::move(payload),
        };
    }

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

    // Нормированная корреляция с символом PSS в каждой позиции, берётся лучшая; перед PSS должен помещаться SSS
    PssPeak findPssPeak(std::span<const Sample_t> signal) const
    {
        const size_t pssSize = m_pssSymbol.size();
        double pssEnergy = 0.0;
        for (const auto& sample : m_pssSymbol)
        {
            pssEnergy += std::norm(sample);
        }

        PssPeak peak;
        for (size_t position = sssToPssDistance; position + pssSize <= signal.size(); ++position)
        {
            Sample_t product;
            double energy = 0.0;
            for (size_t index = 0; index < pssSize; ++index)
            {
                product += std::conj(m_pssSymbol[index]) * signal[position + index];
                energy += std::norm(signal[position + index]);
            }
            // По неравенству Коши — Буняковского значение не больше 1
            const double correlation = energy > 0.0 ? std::sqrt(std::norm(product) / (pssEnergy * energy)) : 0.0;
            if (correlation > peak.correlation)
            {
                peak.position = position;
                peak.correlation = correlation;
            }
        }
        return peak;
    }

    // Фаза между корреляциями двух половин PSS растёт на 2*pi*f*T, T — половина длительности
    double estimateFrequencyOffset(std::span<const Sample_t> signal, size_t pssStart) const
    {
        const size_t halfSize = m_pssSymbol.size() / 2;
        Sample_t firstHalf;
        Sample_t secondHalf;
        for (size_t index = 0; index < halfSize; ++index)
        {
            firstHalf += std::conj(m_pssSymbol[index]) * signal[pssStart + index];
            secondHalf += std::conj(m_pssSymbol[halfSize + index]) * signal[pssStart + halfSize + index];
        }
        const double phase = std::arg(secondHalf * std::conj(firstHalf));
        return phase * m_sampleRateHz / (2.0 * std::numbers::pi * double(halfSize));
    }

    // SSS в субкадрах 0 и 5 разный (TS 36.211 V16.2.0, 6.11.2.1): канал оценивается по PSS, SSS сравнивается с обоими
    // вариантами; возвращает индекс в sssSubframes
    size_t detectHalfFrame(std::span<const Sample_t> signal, size_t pssStart)
    {
        const size_t sssStart = pssStart - sssToPssDistance;
        const Subcarriers_t pss =
            m_modem->Demodulate(signal.subspan(pssStart + cyclicPrefixLength(pssSymbol), fftSize));
        const Subcarriers_t sss =
            m_modem->Demodulate(signal.subspan(sssStart + cyclicPrefixLength(sssSymbol), fftSize));

        std::array<double, sssSubframes.size()> metrics{};
        for (size_t half = 0; half < sssSubframes.size(); ++half)
        {
            Sample_t sum;
            for (size_t index = 0; index < syncLength; ++index)
            {
                const size_t subcarrier = syncFirstSubcarrier + index;
                const Sample_t channel = pss[subcarrier] / m_pss[index];
                sum += sss[subcarrier] * std::conj(channel) * std::conj(m_sss[half][index]);
            }
            metrics[half] = sum.real();
        }
        const size_t half = metrics[1] > metrics[0] ? 1 : 0;
        m_logger->Trace(std::format("SSS metrics: subframe 0 {:.3g}, subframe 5 {:.3g} -> subframe {}", metrics[0],
                                    metrics[1], sssSubframes[half]));
        return half;
    }

    std::unique_ptr<OfdmModem> m_modem;
    std::unique_ptr<TurboCodec> m_turbo;
    std::shared_ptr<Logging::ILogger> m_logger;
    double m_sampleRateHz;
    ModulationAndCoding m_modulationAndCoding;
    Segmentation m_segmentation;
    Signal_t m_pss;
    Signal_t m_pssSymbol;
    std::array<Signal_t, sssSubframes.size()> m_sss;
    std::vector<SubframeCoding> m_coding;
};

} // namespace

std::unique_ptr<IPhyLayer> createPhyLayer(const PhyConfig& config, std::shared_ptr<Logging::ILogger> logger)
{
    if (logger == nullptr)
    {
        return nullptr;
    }

    const auto modulationAndCoding = findModulationAndCoding(config.mcsIndex);
    if (!modulationAndCoding.has_value())
    {
        logger->Error(std::format("Cannot create PHY layer: MCS index {} is out of 0..{}", config.mcsIndex,
                                  maxMcsIndex));
        return nullptr;
    }
    const size_t blockBits = modulationAndCoding->transportBlockSize + crcLength;
    const auto segmentation = segmentTransportBlock(blockBits);
    if (!segmentation.has_value())
    {
        logger->Error(std::format("Cannot create PHY layer: transport block of {} bits cannot be segmented",
                                  blockBits));
        return nullptr;
    }
    if (!isSupportedModulation(modulationAndCoding->modulationOrder))
    {
        logger->Error(std::format("Cannot create PHY layer: MCS {} has unsupported modulation order Q_m = {}",
                                  config.mcsIndex, modulationAndCoding->modulationOrder));
        return nullptr;
    }

    auto modem = OfdmModem::Create();
    if (modem == nullptr)
    {
        logger->Error(std::format("Cannot create PHY layer: FFTW planning of size {} failed", fftSize));
        return nullptr;
    }

    auto turbo = TurboCodec::Create(segmentation->blockSize);
    if (turbo == nullptr)
    {
        logger->Error(std::format("Cannot create PHY layer: turbo codec for block size {} failed",
                                  segmentation->blockSize));
        return nullptr;
    }

    logger->Info(std::format("MCS {}: Q_m = {}, transport block {} bits, {} code block(s) of K = {}", config.mcsIndex,
                             modulationAndCoding->modulationOrder, modulationAndCoding->transportBlockSize,
                             segmentation->blockCount, segmentation->blockSize));
    return std::make_unique<PhyLayer>(std::move(modem), std::move(turbo), std::move(logger), config.sampleRateHz,
                                      *modulationAndCoding, *segmentation);
}

} // namespace Radiocom::SignalManipulation
