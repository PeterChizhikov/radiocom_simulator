#include "Ofdm.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include <fftw3.h>


namespace Radiocom::SignalManipulation
{
namespace
{

constexpr size_t zadoffChuLength = 63;
constexpr size_t zadoffChuRoot = 25;

constexpr size_t maxResourceBlocks = 110;
constexpr size_t resourceBlocks = subcarrierCount / 12;
constexpr size_t referencesPerSymbol = 2 * resourceBlocks;
constexpr size_t goldOffset = 1600;

// Последовательность Задова — Чу, как PSS в LTE (TS 36.211, 6.11.1.1)
Signal_t generateSyncSequence()
{
    Signal_t sequence(syncLength);
    for (size_t index = 0; index < syncLength; ++index)
    {
        // Показатель exp(-j*pi*e / 63) повторяется с периодом 2 * 63, поэтому e берётся по модулю
        const size_t product = index < syncLength / 2 ? index * (index + 1) : (index + 1) * (index + 2);
        const size_t exponent = zadoffChuRoot * product % (2 * zadoffChuLength);
        sequence[index] = std::polar(1.0, -std::numbers::pi * double(exponent) / double(zadoffChuLength));
    }
    return sequence;
}

// Пилоты символа слота (TS 36.211, 6.10.1.1): r(m'), m' = m + 110 - 15
Signal_t generateReferenceSequence(size_t symbol)
{
    constexpr size_t normalCyclicPrefix = 1;
    const uint32_t initialValue = uint32_t((1U << 10) * (7 * (slotNumber + 1) + symbol + 1) * (2 * cellId + 1) +
                                           2 * cellId + normalCyclicPrefix);
    const auto gold = generateGoldSequence(initialValue, 4 * maxResourceBlocks);

    Signal_t sequence(referencesPerSymbol);
    const size_t firstIndex = maxResourceBlocks - resourceBlocks;
    for (size_t index = 0; index < referencesPerSymbol; ++index)
    {
        const size_t m = firstIndex + index;
        sequence[index] = std::sqrt(0.5) * Sample_t(1.0 - 2.0 * gold[2 * m], 1.0 - 2.0 * gold[2 * m + 1]);
    }
    return sequence;
}

// Линейная интерполяция по частоте между пилотами, за крайними пилотами — линейная экстраполяция
Subcarriers_t interpolateInFrequency(std::span<const size_t> positions, std::span<const Sample_t> gains)
{
    Subcarriers_t channel;
    for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
    {
        const auto next = std::upper_bound(positions.begin(), positions.end(), subcarrier);
        const size_t upper = std::clamp(size_t(next - positions.begin()), size_t(1), positions.size() - 1);
        const size_t lower = upper - 1;
        const double fraction = (double(subcarrier) - double(positions[lower])) /
                                (double(positions[upper]) - double(positions[lower]));
        channel[subcarrier] = gains[lower] + (gains[upper] - gains[lower]) * fraction;
    }
    return channel;
}

} // namespace

std::vector<uint8_t> generateGoldSequence(uint32_t initialValue, size_t length)
{
    const size_t total = goldOffset + length;
    std::vector<uint8_t> first(total + 31, 0);
    std::vector<uint8_t> second(total + 31, 0);
    first[0] = 1;
    for (size_t bit = 0; bit < 31; ++bit)
    {
        second[bit] = (initialValue >> bit) & 1;
    }
    for (size_t index = 0; index < total; ++index)
    {
        first[index + 31] = (first[index + 3] + first[index]) & 1;
        second[index + 31] = (second[index + 3] + second[index + 2] + second[index + 1] + second[index]) & 1;
    }

    std::vector<uint8_t> sequence(length);
    for (size_t index = 0; index < length; ++index)
    {
        sequence[index] = (first[index + goldOffset] + second[index + goldOffset]) & 1;
    }
    return sequence;
}

void mapSyncSignal(ResourceGrid_t& grid)
{
    const Signal_t sequence = generateSyncSequence();
    std::copy(sequence.begin(), sequence.end(), grid[syncSymbol].begin() + std::ptrdiff_t(syncFirstSubcarrier));
}

void mapReferenceSignals(ResourceGrid_t& grid)
{
    for (const size_t symbol : referenceSymbols)
    {
        const Signal_t sequence = generateReferenceSequence(symbol);
        for (size_t index = 0; index < referencesPerSymbol; ++index)
        {
            grid[symbol][referenceSpacing * index + referenceShift(symbol)] = sequence[index];
        }
    }
}

ResourceGrid_t estimateChannel(const ResourceGrid_t& received)
{
    ResourceGrid_t reference{};
    mapReferenceSignals(reference);

    std::array<Subcarriers_t, referenceSymbols.size()> estimates;
    for (size_t index = 0; index < referenceSymbols.size(); ++index)
    {
        const size_t symbol = referenceSymbols[index];
        std::vector<size_t> positions;
        Signal_t gains;
        for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
        {
            if (isReferenceElement(symbol, subcarrier))
            {
                positions.push_back(subcarrier);
                gains.push_back(received[symbol][subcarrier] / reference[symbol][subcarrier]);
            }
        }
        estimates[index] = interpolateInFrequency(positions, gains);
    }

    ResourceGrid_t channel{};
    const double symbolDistance = double(referenceSymbols[1]) - double(referenceSymbols[0]);
    for (size_t symbol = 0; symbol < symbolsPerSlot; ++symbol)
    {
        const double weight = (double(symbol) - double(referenceSymbols[0])) / symbolDistance;
        for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
        {
            channel[symbol][subcarrier] =
                estimates[0][subcarrier] + (estimates[1][subcarrier] - estimates[0][subcarrier]) * weight;
        }
    }
    return channel;
}

FftwPlan::FftwPlan(fftw_plan_s* plan) noexcept
    : m_plan(plan)
{
}

FftwPlan::~FftwPlan()
{
    if (m_plan != nullptr)
    {
        fftw_destroy_plan(m_plan);
    }
}

FftwPlan::FftwPlan(FftwPlan&& other) noexcept
    : m_plan(std::exchange(other.m_plan, nullptr))
{
}

FftwPlan& FftwPlan::operator=(FftwPlan&& other) noexcept
{
    std::swap(m_plan, other.m_plan);
    return *this;
}

void FftwPlan::Execute() const
{
    fftw_execute(m_plan);
}

std::unique_ptr<OfdmModem> OfdmModem::Create()
{
    auto modem = std::unique_ptr<OfdmModem>(new OfdmModem());

    auto* frequencyData = reinterpret_cast<fftw_complex*>(modem->m_frequencyBuffer.data());
    auto* timeData = reinterpret_cast<fftw_complex*>(modem->m_timeBuffer.data());
    modem->m_inversePlan =
        FftwPlan(fftw_plan_dft_1d(int(fftSize), frequencyData, timeData, FFTW_BACKWARD, FFTW_ESTIMATE));
    modem->m_forwardPlan =
        FftwPlan(fftw_plan_dft_1d(int(fftSize), timeData, frequencyData, FFTW_FORWARD, FFTW_ESTIMATE));

    if (!modem->m_inversePlan.IsValid() || !modem->m_forwardPlan.IsValid())
    {
        return nullptr;
    }
    return modem;
}

OfdmModem::OfdmModem()
    : m_frequencyBuffer(fftSize)
    , m_timeBuffer(fftSize)
{
}

Signal_t OfdmModem::Modulate(std::span<const Sample_t> subcarriers, size_t cyclicPrefix)
{
    std::fill(m_frequencyBuffer.begin(), m_frequencyBuffer.end(), Sample_t());
    for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
    {
        m_frequencyBuffer[frequencyBin(subcarrier)] = subcarriers[subcarrier];
    }

    m_inversePlan.Execute();
    // FFTW не нормирует обратное преобразование
    for (auto& sample : m_timeBuffer)
    {
        sample /= double(fftSize);
    }

    Signal_t symbol;
    symbol.reserve(cyclicPrefix + fftSize);
    symbol.insert(symbol.end(), m_timeBuffer.end() - std::ptrdiff_t(cyclicPrefix), m_timeBuffer.end());
    symbol.insert(symbol.end(), m_timeBuffer.begin(), m_timeBuffer.end());
    return symbol;
}

Subcarriers_t OfdmModem::Demodulate(std::span<const Sample_t> usefulPart)
{
    std::copy(usefulPart.begin(), usefulPart.end(), m_timeBuffer.begin());
    m_forwardPlan.Execute();

    Subcarriers_t subcarriers;
    for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
    {
        subcarriers[subcarrier] = m_frequencyBuffer[frequencyBin(subcarrier)];
    }
    return subcarriers;
}

} // namespace Radiocom::SignalManipulation
