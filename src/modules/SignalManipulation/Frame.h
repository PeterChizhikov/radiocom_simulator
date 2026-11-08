#pragma once

#include "Ofdm.h"
#include "Types.h"

#include <array>
#include <cstddef>


namespace Radiocom::SignalManipulation
{

// Кадр типа 2 (TDD): 10 субкадров по 1 мс (TS 36.211 V16.2.0, 4.2)
inline constexpr size_t subframesPerFrame = 10;
inline constexpr size_t frameLength = subframesPerFrame * subframeLength;

enum class SubframeType
{
    Downlink,
    Special,
    Uplink,
};

// Конфигурация UL/DL 1: D S U U D D S U U D (TS 36.211 V16.2.0, табл. 4.2-2)
inline constexpr std::array<SubframeType, subframesPerFrame> subframeTypes = {
    SubframeType::Downlink, SubframeType::Special, SubframeType::Uplink,   SubframeType::Uplink,
    SubframeType::Downlink, SubframeType::Downlink, SubframeType::Special, SubframeType::Uplink,
    SubframeType::Uplink,   SubframeType::Downlink,
};
// Субкадры D, в каждом один транспортный блок
inline constexpr std::array<size_t, 4> dataSubframes = {0, 4, 5, 9};

// Специальный субкадр конфигурации 0: DwPTS = 6592 Ts, то есть 3 символа с обычным CP (TS 36.211 V16.2.0,
// табл. 4.2-1); GP и UpPTS не передаются, данных в DwPTS нет
inline constexpr size_t downlinkPilotSymbols = 3;
static_assert(symbolOffset(downlinkPilotSymbols) * 8 == 6592);

// PSS — третий символ субкадров 1 и 6 (TS 36.211 V16.2.0, 6.11.1.2); SSS — последний символ слотов 1 и 11,
// то есть символ 13 субкадров 0 и 5 (6.11.2.2)
inline constexpr std::array<size_t, 2> pssSubframes = {1, 6};
inline constexpr size_t pssSymbol = 2;
inline constexpr std::array<size_t, 2> sssSubframes = {0, 5};
inline constexpr size_t sssSymbol = symbolsPerSubframe - 1;

// d(n), n = 0..61, на поднесущих k = n - 31 + 90; элементы n = -5..-1 и 62..66 зарезервированы
// (TS 36.211 V16.2.0, 6.11.1.2, 6.11.2.2)
inline constexpr size_t syncLength = 62;
inline constexpr size_t syncFirstSubcarrier = subcarrierCount / 2 - syncLength / 2;
inline constexpr size_t syncReservedBegin = syncFirstSubcarrier - 5;
inline constexpr size_t syncReservedEnd = syncFirstSubcarrier + syncLength + 5;

// Индекс отсчёта кадра, с которого начинается циклический префикс символа субкадра
constexpr size_t frameSymbolOffset(size_t subframe, size_t symbol)
{
    return subframe * subframeLength + symbolOffset(symbol);
}

// PSS d_u(n) (TS 36.211 V16.2.0, 6.11.1.1)
Signal_t generatePss();
// SSS d(n) для субкадра 0 или 5 (TS 36.211 V16.2.0, 6.11.2.1)
Signal_t generateSss(size_t subframe);

// CRS, PSS и SSS субкадра; в специальном субкадре только DwPTS, в субкадре U ничего
void mapSignals(ResourceGrid_t& grid, size_t subframe);

// Ресурсный элемент несёт PDSCH: субкадр D, элемент не занят CRS и синхросигналом (TS 36.211 V16.2.0, 6.3.5)
bool isDataElement(size_t subframe, size_t symbol, size_t subcarrier);
size_t countDataElements(size_t subframe);

} // namespace Radiocom::SignalManipulation
