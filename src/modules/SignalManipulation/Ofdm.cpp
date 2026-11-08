#include "Ofdm.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <fftw3.h>


namespace Radiocom::SignalManipulation
{
namespace
{

constexpr size_t maxResourceBlocks = 110;
constexpr size_t resourceBlocks = subcarrierCount / 12;
constexpr size_t referencesPerSymbol = 2 * resourceBlocks;
constexpr size_t goldOffset = 1600;

// Пилоты символа субкадра (TS 36.211 V16.2.0, 6.10.1.1): r(m'), m' = m + 110 - 15,
// c_init = 2^10 * (7 * (n_s + 1) + l + 1) * (2 * N_ID^cell + 1) + 2 * N_ID^cell + N_CP
Signal_t generateReferenceSequence(size_t subframe, size_t symbol)
{
    constexpr size_t normalCyclicPrefix = 1;
    const size_t slot = slotsPerSubframe * subframe + symbol / symbolsPerSlot;
    const size_t slotSymbol = symbol % symbolsPerSlot;
    const uint32_t initialValue = uint32_t((1U << 10) * (7 * (slot + 1) + slotSymbol + 1) * (2 * cellId + 1) +
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

// Линейная интерполяция между соседними узлами, за крайними узлами — линейная экстраполяция
template <typename Value_t>
Value_t interpolate(std::span<const size_t> positions, std::span<const Value_t> values, size_t point)
{
    const auto next = std::upper_bound(positions.begin(), positions.end(), point);
    const size_t upper = std::clamp(size_t(next - positions.begin()), size_t(1), positions.size() - 1);
    const size_t lower = upper - 1;
    const double fraction =
        (double(point) - double(positions[lower])) / (double(positions[upper]) - double(positions[lower]));
    return values[lower] + (values[upper] - values[lower]) * fraction;
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

void mapReferenceSignals(ResourceGrid_t& grid, size_t subframe, size_t symbolCount)
{
    for (const size_t symbol : referenceSymbols)
    {
        if (symbol >= symbolCount)
        {
            continue;
        }
        const Signal_t sequence = generateReferenceSequence(subframe, symbol);
        for (size_t index = 0; index < referencesPerSymbol; ++index)
        {
            grid[symbol][referenceSpacing * index + referenceShift(symbol)] = sequence[index];
        }
    }
}

ResourceGrid_t estimateChannel(const ResourceGrid_t& received, size_t subframe)
{
    ResourceGrid_t reference{};
    mapReferenceSignals(reference, subframe, symbolsPerSubframe);

    std::array<Signal_t, subcarrierCount> estimates;
    for (const size_t symbol : referenceSymbols)
    {
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
        for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
        {
            estimates[subcarrier].push_back(interpolate<Sample_t>(positions, gains, subcarrier));
        }
    }

    ResourceGrid_t channel{};
    for (size_t symbol = 0; symbol < symbolsPerSubframe; ++symbol)
    {
        for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
        {
            channel[symbol][subcarrier] = interpolate<Sample_t>(referenceSymbols, estimates[subcarrier], symbol);
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

Signal_t OfdmModem::ModulateSubframe(const ResourceGrid_t& grid)
{
    Signal_t subframe;
    subframe.reserve(subframeLength);
    for (size_t symbol = 0; symbol < symbolsPerSubframe; ++symbol)
    {
        const Signal_t samples = Modulate(grid[symbol], cyclicPrefixLength(symbol));
        subframe.insert(subframe.end(), samples.begin(), samples.end());
    }
    return subframe;
}

ResourceGrid_t OfdmModem::DemodulateSubframe(std::span<const Sample_t> subframe)
{
    ResourceGrid_t grid;
    for (size_t symbol = 0; symbol < symbolsPerSubframe; ++symbol)
    {
        grid[symbol] = Demodulate(subframe.subspan(symbolOffset(symbol) + cyclicPrefixLength(symbol), fftSize));
    }
    return grid;
}

} // namespace Radiocom::SignalManipulation
