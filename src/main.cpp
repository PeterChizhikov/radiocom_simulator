#include "Logger.h"
#include "SimulationCore.h"
#include <iostream>

int main(int argc, char **argv) {
  try {
    SimulationCore runner;
  } catch (const std::exception &e) {
    Logger::getInstance().error(
        std::string("Program didn't finish! Error occured: ") + e.what());
  }
  return 0;
}