#include "SimulationCore.h"

void SimulationCore::runSimulation() {
    char testCase[31] = "testInfoRunSimulator!";
    std::vector<std::tuple<double, bool, double>> results;
    //Исследование на разных значениях сигнал-шум:
    for(double snr = -40.0; snr < 40; snr+=0.5){
        Logger::getInstance().info("Simulation started");
        SignalManipulation SignalManipulationProc("Hamming");
        Logger::getInstance().info("Signal Encoding started");
        SignalManipulationProc.startEncodingManipulation(testCase, snr);
        Logger::getInstance().info("Signal Encoding ended");
        //-------------Other-----------------------
        Logger::getInstance().info("Signal Decoding started");
        bool tempRes = false;
        double BER = 0.0;
        SignalManipulationProc.startDecodingManipulation(tempRes, BER);
        results.emplace_back(std::make_tuple(snr, tempRes, BER));
        Logger::getInstance().info("Signal Decoding ended");
    }

    Logger::getInstance().info("==================Result block==================");
    for(const auto& [value, flag, ber] : results){
        std::string status = flag ? "success" : "error";
        std::string resString = "snr: " + std::to_string(value)  + ", BER: " + std::to_string(ber) + ", status: " + status;
        Logger::getInstance().info(resString);
    }

    
}