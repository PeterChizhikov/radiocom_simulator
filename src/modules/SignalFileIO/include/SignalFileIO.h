#pragma once

#include "Logger.h"
#include <fstream>
#include <vector>
#include <complex>
#include <string>

class SignalFileIO {
public:
    SignalFileIO() = delete;

    static void saveToBin(const std::vector<std::complex<double>>& signal, const std::string& filename);
    static std::vector<std::complex<double>> readFromBin(const std::string& filename);
};
