#include "Logger.h"
#include "SimulationCore.h"
#include <iostream>

int main(int argc, char **argv) {
    try {
        SimulationCore simCore;
        simCore.runSimulation();
    } catch (...) {
        Logger::getInstance().error("Program didn't finish! Error occured: ");
    }
    return 0;
}