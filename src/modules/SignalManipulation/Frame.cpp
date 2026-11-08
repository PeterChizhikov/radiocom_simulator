#include "Frame.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>


namespace Radiocom::SignalManipulation
{
namespace
{

constexpr size_t zadoffChuLength = 63;
// Корни 25, 29, 34 для N_ID^(2) = 0, 1, 2 (TS 36.211 V16.2.0, табл. 6.11.1.1-1)
constexpr std::array<size_t, 3> zadoffChuRoots = {25, 29, 34};

constexpr size_t sssHalfLength = 31;

// ~s, ~c, ~z: x(i + 5) = сумма x(i + tap) по модулю 2, x(0..3) = 0, x(4) = 1; значение 1 - 2 * x(i)
// (TS 36.211 V16.2.0, 6.11.2.1)
std::array<int, sssHalfLength> generateMSequence(std::span<const size_t> taps)
{
    std::array<uint8_t, sssHalfLength> state{};
    state[4] = 1;
    for (size_t index = 0; index + 5 < sssHalfLength; ++index)
    {
        uint8_t sum = 0;
        for (const size_t tap : taps)
        {
            sum ^= state[index + tap];
        }
        state[index + 5] = sum;
    }

    std::array<int, sssHalfLength> sequence{};
    for (size_t index = 0; index < sssHalfLength; ++index)
    {
        sequence[index] = 1 - 2 * int(state[index]);
    }
    return sequence;
}

bool isSyncSymbol(size_t subframe, size_t symbol)
{
    const bool isPss = symbol == pssSymbol && std::ranges::find(pssSubframes, subframe) != pssSubframes.end();
    const bool isSss = symbol == sssSymbol && std::ranges::find(sssSubframes, subframe) != sssSubframes.end();
    return isPss || isSss;
}

} // namespace

Signal_t generatePss()
{
    const size_t root = zadoffChuRoots[cellSector];
    Signal_t sequence(syncLength);
    for (size_t index = 0; index < syncLength; ++index)
    {
        // Показатель exp(-j*pi*e / 63) повторяется с периодом 2 * 63, поэтому e берётся по модулю
        const size_t product = index < syncLength / 2 ? index * (index + 1) : (index + 1) * (index + 2);
        const size_t exponent = root * product % (2 * zadoffChuLength);
        sequence[index] = std::polar(1.0, -std::numbers::pi * double(exponent) / double(zadoffChuLength));
    }
    return sequence;
}

Signal_t generateSss(size_t subframe)
{
    constexpr std::array<size_t, 2> sTaps = {0, 2};
    constexpr std::array<size_t, 2> cTaps = {0, 3};
    constexpr std::array<size_t, 4> zTaps = {0, 1, 2, 4};
    const auto s = generateMSequence(sTaps);
    const auto c = generateMSequence(cTaps);
    const auto z = generateMSequence(zTaps);

    // m0, m1 по N_ID^(1) (TS 36.211 V16.2.0, 6.11.2.1, табл. 6.11.2.1-1)
    const size_t qPrime = cellGroup / 30;
    const size_t q = (cellGroup + qPrime * (qPrime + 1) / 2) / 30;
    const size_t mPrime = cellGroup + q * (q + 1) / 2;
    const size_t m0 = mPrime % sssHalfLength;
    const size_t m1 = (m0 + mPrime / sssHalfLength + 1) % sssHalfLength;

    const bool isFirstHalf = subframe < subframesPerFrame / 2;
    Signal_t sequence(syncLength);
    for (size_t n = 0; n < sssHalfLength; ++n)
    {
        const int s0 = s[(n + m0) % sssHalfLength];
        const int s1 = s[(n + m1) % sssHalfLength];
        const int c0 = c[(n + cellSector) % sssHalfLength];
        const int c1 = c[(n + cellSector + 3) % sssHalfLength];
        const int z1m0 = z[(n + m0 % 8) % sssHalfLength];
        const int z1m1 = z[(n + m1 % 8) % sssHalfLength];
        sequence[2 * n] = isFirstHalf ? s0 * c0 : s1 * c0;
        sequence[2 * n + 1] = isFirstHalf ? s1 * c1 * z1m0 : s0 * c1 * z1m1;
    }
    return sequence;
}

void mapSignals(ResourceGrid_t& grid, size_t subframe)
{
    switch (subframeTypes[subframe])
    {
        case SubframeType::Downlink:
            mapReferenceSignals(grid, subframe, symbolsPerSubframe);
            break;
        case SubframeType::Special:
            mapReferenceSignals(grid, subframe, downlinkPilotSymbols);
            break;
        case SubframeType::Uplink:
            return;
    }

    const auto place = [&](size_t symbol, const Signal_t& sequence)
    { std::ranges::copy(sequence, grid[symbol].begin() + std::ptrdiff_t(syncFirstSubcarrier)); };
    if (std::ranges::find(pssSubframes, subframe) != pssSubframes.end())
    {
        place(pssSymbol, generatePss());
    }
    if (std::ranges::find(sssSubframes, subframe) != sssSubframes.end())
    {
        place(sssSymbol, generateSss(subframe));
    }
}

bool isDataElement(size_t subframe, size_t symbol, size_t subcarrier)
{
    if (subframeTypes[subframe] != SubframeType::Downlink || isReferenceElement(symbol, subcarrier))
    {
        return false;
    }
    return !isSyncSymbol(subframe, symbol) || subcarrier < syncReservedBegin || subcarrier >= syncReservedEnd;
}

size_t countDataElements(size_t subframe)
{
    size_t count = 0;
    for (size_t symbol = 0; symbol < symbolsPerSubframe; ++symbol)
    {
        for (size_t subcarrier = 0; subcarrier < subcarrierCount; ++subcarrier)
        {
            count += isDataElement(subframe, symbol, subcarrier) ? 1 : 0;
        }
    }
    return count;
}

} // namespace Radiocom::SignalManipulation
