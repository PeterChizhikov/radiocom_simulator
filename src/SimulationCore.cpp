#include "SimulationCore.h"

void SimulationCore::runSimulation() {
    Logger::getInstance().info("Simulation started");
    SignalManipulation SignalManipulationProc;
    Logger::getInstance().info("Signal Encoding started");
    SignalManipulationProc.startEncodingManipulation();
    Logger::getInstance().info("Signal Encoding ended");
    //-------------Other-----------------------
    Logger::getInstance().info("Signal Decoding started");
    SignalManipulationProc.startDecodingManipulation();
    Logger::getInstance().info("Signal Decoding ended");
}