#pragma once
#include "Hamming.h"
#include "SignalFileIO.h"
#include <iostream>
#include <string_view>
#include <cstring>
#include <vector>
#include <complex>
#include <fftw3.h>
#include <fstream>
#include "Logger.h"

struct QPSKSymbol {
    std::complex<double> value;
};

class SignalManipulation {
public:
  SignalManipulation()=default;

  void startEncodingManipulation(char* info);
  void startDecodingManipulation();

  ~SignalManipulation()=default;
private:
  std::vector<QPSKSymbol> mapToQPSK(const std::vector<uint8_t>& bits);
  std::vector<std::complex<double>> mapToSubcarriers(const std::vector<QPSKSymbol>& qpskSymbols);
  std::vector<std::complex<double>> createOfdmSymbol(const std::vector<std::complex<double>>& fftBins);
  std::vector<uint8_t> encodeWithHamming(char* info);
  void decodeWithHamming(std::vector<uint8_t> signal);
  void printBits(const uint8_t* data, size_t size, const std::string& prefix);
  std::vector<std::complex<double>> demodulateOfdmSymbol(const std::vector<std::complex<double>>& ofdmSymbolWithCP);
  std::vector<QPSKSymbol> demapFromSubcarriers(const std::vector<std::complex<double>>& fftBins, size_t expectedSymbolsCount);
  std::vector<uint8_t> demapFromQPSK(const std::vector<QPSKSymbol>& qpskSymbols);
};