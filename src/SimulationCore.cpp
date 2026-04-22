#include "SimulationCore.h"

void SimulationCore::runSimulation() {
    char testCase[31] = "Hamming";

    Logger::getInstance().info("Simulation started");
    SignalManipulation SignalManipulationProc;
    Logger::getInstance().info("Signal Encoding started");
    SignalManipulationProc.startEncodingManipulation(testCase);
    Logger::getInstance().info("Signal Encoding ended");
    //-------------Other-----------------------
    Logger::getInstance().info("Signal Decoding started");
    SignalManipulationProc.startDecodingManipulation();
    Logger::getInstance().info("Signal Decoding ended");
}