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
#include <random>
#include <cmath>
#include "Logger.h"

struct QPSKSymbol {
    std::complex<double> value;
};

class SignalManipulation {
public:
  SignalManipulation(std::string codeType);

  void startEncodingManipulation(char* info, double snr);
  void startDecodingManipulation(bool &resultStatus, double &BER);

  ~SignalManipulation()=default;
private:
  std::vector<uint8_t> originalBytes;
  std::string codeType;
  std::vector<QPSKSymbol> mapToQPSK(const std::vector<uint8_t>& bits);
  std::vector<std::complex<double>> mapToSubcarriers(const std::vector<QPSKSymbol>& qpskSymbols);
  std::vector<std::complex<double>> createOfdmSymbol(const std::vector<std::complex<double>>& fftBins);
  std::vector<uint8_t> encodeWithHamming(char* info);
  void decodeWithHamming(std::vector<uint8_t> signal);
  void printBits(const uint8_t* data, size_t size, const std::string& prefix);
  std::vector<std::complex<double>> demodulateOfdmSymbol(const std::vector<std::complex<double>>& ofdmSymbolWithCP);
  std::vector<QPSKSymbol> demapFromSubcarriers(const std::vector<std::complex<double>>& fftBins, size_t expectedSymbolsCount);
  std::vector<uint8_t> demapFromQPSK(const std::vector<QPSKSymbol>& qpskSymbols);
  std::vector<std::complex<double>> addAWGN(const std::vector<std::complex<double>>& signal, double snr_db);
  double calculateBER(const std::vector<uint8_t>& original, const std::vector<uint8_t>& received);
};