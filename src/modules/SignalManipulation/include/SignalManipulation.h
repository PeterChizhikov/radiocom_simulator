#pragma once
#include "Hamming.h"
#include "SignalFileIO.h"
#include "Types.h"
#include "Logger.h"
#include <iostream>
#include <string_view>
#include <cstring>
#include <vector>
#include <complex>
#include <fftw3.h>
#include <fstream>
#include <random>
#include <cmath>

struct QPSKSymbol {
    std::complex<double> value;
};

class SignalManipulation {
public:
  SignalManipulation(std::string codeType);

  void startEncodingManipulation(char* info, const double snr);
  void startDecodingManipulation(bool &resultStatus, double &BER);

  ~SignalManipulation()=default;
private:
  std::vector<uint8_t> originalBytes;
  std::string codeType;
  std::vector<QPSKSymbol> mapToQPSK(const std::vector<uint8_t>& bits);
  SignalType mapToSubcarriers(const std::vector<QPSKSymbol>& qpskSymbols);
  SignalType createOfdmSymbol(const SignalType& fftBins);
  std::vector<uint8_t> encodeWithHamming(char* info);
  void decodeWithHamming(std::vector<uint8_t> signal);
  void printBits(const uint8_t* data, const size_t size, const std::string& prefix);
  SignalType demodulateOfdmSymbol(const SignalType& ofdmSymbolWithCP);
  std::vector<QPSKSymbol> demapFromSubcarriers(const SignalType& fftBins, size_t expectedSymbolsCount);
  std::vector<uint8_t> demapFromQPSK(const std::vector<QPSKSymbol>& qpskSymbols);
  SignalType addAWGN(const SignalType& signal,const double snr_db);
  double calculateBER(const std::vector<uint8_t>& original, const std::vector<uint8_t>& received);
};