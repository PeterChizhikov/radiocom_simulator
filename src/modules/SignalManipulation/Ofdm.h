#pragma once

#include "Types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>


struct fftw_plan_s;

namespace Radiocom::SignalManipulation
{

// Сетка LTE для полосы 3 МГц: 15 RB по 12 поднесущих, слот из 7 символов с обычным CP (TS 36.211, 6.2.3, 6.12)
inline constexpr size_t fftSize = 256;
inline constexpr size_t subcarrierCount = 180;
inline constexpr size_t symbolsPerSlot = 7;

constexpr size_t cyclicPrefixLength(size_t symbol)
{
    return symbol == 0 ? 20 : 18;
}

// Индекс отсчёта слота, с которого начинается циклический префикс символа
constexpr size_t symbolOffset(size_t symbol)
{
    size_t offset = 0;
    for (size_t previous = 0; previous < symbol; ++previous)
    {
        offset += cyclicPrefixLength(previous) + fftSize;
    }
    return offset;
}

inline constexpr size_t slotLength = symbolOffset(symbolsPerSlot);

// Поднесущие -90..-1 лежат в нижней половине строки сетки, 1..90 — в верхней, DC пропущена (TS 36.211, 6.12)
constexpr size_t frequencyBin(size_t subcarrier)
{
    constexpr size_t half = subcarrierCount / 2;
    return subcarrier < half ? fftSize - half + subcarrier : subcarrier - half + 1;
}

using Subcarriers_t = std::array<Sample_t, subcarrierCount>;
using ResourceGrid_t = std::array<Subcarriers_t, symbolsPerSlot>;

inline constexpr size_t cellId = 0;
inline constexpr size_t slotNumber = 0;

// PSS: последний символ слота 0, 62 поднесущие вокруг DC: k = n - 31 + 90 (TS 36.211, 6.11.1.2)
inline constexpr size_t syncSymbol = symbolsPerSlot - 1;
inline constexpr size_t syncLength = 62;
inline constexpr size_t syncFirstSubcarrier = subcarrierCount / 2 - syncLength / 2;

// CRS порта 0 (TS 36.211, 6.10.1): символы 0 и 4 слота, каждая 6-я поднесущая, во втором символе со сдвигом 3
inline constexpr size_t referenceSpacing = 6;
inline constexpr std::array<size_t, 2> referenceSymbols = {0, symbolsPerSlot - 3};

constexpr size_t referenceShift(size_t symbol)
{
    const size_t shift = symbol == 0 ? 0 : 3;
    return (shift + cellId % referenceSpacing) % referenceSpacing;
}

constexpr bool isReferenceElement(size_t symbol, size_t subcarrier)
{
    return (symbol == referenceSymbols[0] || symbol == referenceSymbols[1]) &&
           subcarrier % referenceSpacing == referenceShift(symbol);
}

// Ресурсный элемент несёт данные, если он не занят синхросигналом и опорными сигналами
constexpr bool isDataElement(size_t symbol, size_t subcarrier)
{
    return symbol != syncSymbol && !isReferenceElement(symbol, subcarrier);
}

// Последовательность Голда длиной 31 (TS 36.211, 7.2)
std::vector<uint8_t> generateGoldSequence(uint32_t initialValue, size_t length);

// PSS (TS 36.211, 6.11.1) в символе syncSymbol
void mapSyncSignal(ResourceGrid_t& grid);
// CRS порта 0 (TS 36.211, 6.10.1) в символах referenceSymbols
void mapReferenceSignals(ResourceGrid_t& grid);

// Оценка канала по CRS: отношение принятого к известному в пилотах, интерполяция по частоте, затем по времени
ResourceGrid_t estimateChannel(const ResourceGrid_t& received);

// Владеет планом FFTW (fftw_plan — указатель на fftw_plan_s)
class FftwPlan
{
public:
    FftwPlan() = default;
    explicit FftwPlan(fftw_plan_s* plan) noexcept;
    ~FftwPlan();

    FftwPlan(const FftwPlan& other) = delete;
    FftwPlan(FftwPlan&& other) noexcept;
    FftwPlan& operator=(const FftwPlan& other) = delete;
    FftwPlan& operator=(FftwPlan&& other) noexcept;

    bool IsValid() const { return m_plan != nullptr; }
    void Execute() const;

private:
    fftw_plan_s* m_plan = nullptr;
};

// Модулятор и демодулятор одного OFDM-символа сетки
class OfdmModem
{
public:
    static std::unique_ptr<OfdmModem> Create();

    OfdmModem(const OfdmModem& other) = delete;
    OfdmModem(OfdmModem&& other) = delete;
    OfdmModem& operator=(const OfdmModem& other) = delete;
    OfdmModem& operator=(OfdmModem&& other) = delete;
    ~OfdmModem() = default;

    // Один OFDM-символ: циклический префикс и полезная часть
    Signal_t Modulate(std::span<const Sample_t> subcarriers, size_t cyclicPrefix);
    // usefulPart — fftSize отсчётов после циклического префикса
    Subcarriers_t Demodulate(std::span<const Sample_t> usefulPart);

private:
    OfdmModem();

    Signal_t m_frequencyBuffer;
    Signal_t m_timeBuffer;
    // Планы FFTW привязаны к адресам буферов: объявлены после них, чтобы разрушаться первыми
    FftwPlan m_inversePlan;
    FftwPlan m_forwardPlan;
};

} // namespace Radiocom::SignalManipulation
