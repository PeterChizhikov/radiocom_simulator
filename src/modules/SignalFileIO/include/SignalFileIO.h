#pragma once

#include "Logger.h"
#include "Types.h"
#include <fstream>
#include <vector>
#include <complex>
#include <string>

class SignalFileIO {
public:
    SignalFileIO() = delete;

    static void saveToBin(const SignalType& signal, const std::string& filename);
    static SignalType readFromBin(const std::string& filename);
};
