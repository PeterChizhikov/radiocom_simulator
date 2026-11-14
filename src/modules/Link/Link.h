#pragma once

#include "Logger.h"
#include "Sdr.h"
#include "SignalManipulation.h"

#include <chrono>
#include <cstddef>


namespace Radiocom::Link
{

enum class Role
{
    BaseStation,
    Subscriber,
};

struct LinkConfig
{
    Role role = Role::BaseStation;
    size_t exchangeCount = 100;
    size_t maxAttempts = 1;
    std::chrono::milliseconds transmitDuration{500};
    std::chrono::milliseconds turnaroundDelay{600};
    std::chrono::milliseconds responseTimeout{3000};
    std::chrono::milliseconds idleTimeout{10000};
};

bool runExchange(Sdr::PlutoDevice& device, SignalManipulation::IPhyLayer& phy, const LinkConfig& config,
                 Logging::ILogger& logger);

} // namespace Radiocom::Link
