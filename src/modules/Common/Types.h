#pragma once

#include <complex>
#include <cstdint>
#include <vector>


namespace Radiocom
{

using Sample_t = std::complex<double>;
using Signal_t = std::vector<Sample_t>;
using Bytes_t = std::vector<uint8_t>;

} // namespace Radiocom
