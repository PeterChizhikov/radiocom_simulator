#pragma once
#include "Hamming.h"
#include <iostream>
#include <string_view>
#include <cstring>
#include <vector>

class SignalManipulation {
public:
  SignalManipulation()=default;

  void startEncodingManipulation(char* info);
  void startDecodingManipulation();
private:
  

  std::vector<uint8_t> encodeWithHamming(char* info);
  void decodeWithHamming(std::vector<uint8_t> signal);
};