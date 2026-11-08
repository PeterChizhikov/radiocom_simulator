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

// Сетка LTE для полосы 3 МГц: 15 RB по 12 поднесущих, слот из 7 символов с обычным CP (TS 36.211 V16.2.0, 6.2.3,
// 6.12); субкадр из двух слотов (4.2)
inline constexpr size_t fftSize = 256;
inline constexpr size_t subcarrierCount = 180;
inline constexpr size_t symbolsPerSlot = 7;
inline constexpr size_t slotsPerSubframe = 2;
inline constexpr size_t symbolsPerSubframe = symbolsPerSlot * slotsPerSubframe;

// CP 160 Ts для l = 0 и 144 Ts для l = 1..6 при N = 2048 (TS 36.211 V16.2.0, табл. 6.12-1), при N = 256 в 8 раз меньше;
// symbol — номер символа в субкадре
constexpr size_t cyclicPrefixLength(size_t symbol)
{
    return symbol % symbolsPerSlot == 0 ? 20 : 18;
}

// Индекс отсчёта субкадра, с которого начинается циклический префикс символа
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
inline constexpr size_t subframeLength = symbolOffset(symbolsPerSubframe);

// Поднесущие -90..-1 лежат в нижней половине строки сетки, 1..90 — в верхней, DC пропущена (TS 36.211 V16.2.0, 6.12)
constexpr size_t frequencyBin(size_t subcarrier)
{
    constexpr size_t half = subcarrierCount / 2;
    return subcarrier < half ? fftSize - half + subcarrier : subcarrier - half + 1;
}

using Subcarriers_t = std::array<Sample_t, subcarrierCount>;
using ResourceGrid_t = std::array<Subcarriers_t, symbolsPerSubframe>;

// N_ID^cell = 3 * N_ID^(1) + N_ID^(2) (TS 36.211 V16.2.0, 6.11), в проекте одна ячейка
inline constexpr size_t cellGroup = 0;
inline constexpr size_t cellSector = 0;
inline constexpr size_t cellId = 3 * cellGroup + cellSector;

// CRS порта 0 (TS 36.211 V16.2.0, 6.10.1.2): символы l = 0 и l = N_symb - 3 каждого слота, каждая 6-я поднесущая,
// v = 0 при l = 0 и v = 3 иначе, v_shift = N_ID^cell mod 6
inline constexpr size_t referenceSpacing = 6;
inline constexpr std::array<size_t, 4> referenceSymbols = {0, symbolsPerSlot - 3, symbolsPerSlot,
                                                           2 * symbolsPerSlot - 3};

constexpr bool isReferenceSymbol(size_t symbol)
{
    return symbol % symbolsPerSlot == 0 || symbol % symbolsPerSlot == symbolsPerSlot - 3;
}

constexpr size_t referenceShift(size_t symbol)
{
    const size_t shift = symbol % symbolsPerSlot == 0 ? 0 : 3;
    return (shift + cellId % referenceSpacing) % referenceSpacing;
}

constexpr bool isReferenceElement(size_t symbol, size_t subcarrier)
{
    return isReferenceSymbol(symbol) && subcarrier % referenceSpacing == referenceShift(symbol);
}

// Последовательность Голда длиной 31 (TS 36.211 V16.2.0, 7.2)
std::vector<uint8_t> generateGoldSequence(uint32_t initialValue, size_t length);

// CRS порта 0 субкадра в первых symbolCount символах (меньше 14 для DwPTS)
void mapReferenceSignals(ResourceGrid_t& grid, size_t subframe, size_t symbolCount);

// Оценка канала субкадра по CRS: отношение принятого к известному в пилотах, интерполяция по частоте, затем линейная
// по времени между соседними пилотными символами (за последним — экстраполяция)
ResourceGrid_t estimateChannel(const ResourceGrid_t& received, size_t subframe);

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

    // Субкадр целиком: 14 символов с циклическими префиксами, subframeLength отсчётов
    Signal_t ModulateSubframe(const ResourceGrid_t& grid);
    // subframe — subframeLength отсчётов, начиная с первого отсчёта субкадра
    ResourceGrid_t DemodulateSubframe(std::span<const Sample_t> subframe);

private:
    OfdmModem();

    Signal_t m_frequencyBuffer;
    Signal_t m_timeBuffer;
    // Планы FFTW привязаны к адресам буферов: объявлены после них, чтобы разрушаться первыми
    FftwPlan m_inversePlan;
    FftwPlan m_forwardPlan;
};

} // namespace Radiocom::SignalManipulation
