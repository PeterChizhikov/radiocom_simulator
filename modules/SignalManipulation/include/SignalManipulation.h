#pragma once
#include "Hamming.h"
#include <iostream>
#include <string_view>
#include <cstring>

class SignalManipulation {
public:
  SignalManipulation()=default;

  void startEncodingManipulation();
  void startDecodingManipulation();
private:
  char testCase[31] = "Hello, world! Hamming encoding";

  void encodeWithHamming(char* info);
  void decodeWithHamming();
};