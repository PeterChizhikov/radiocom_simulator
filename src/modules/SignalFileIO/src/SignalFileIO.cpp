#include "SignalFileIO.h"


void SignalFileIO::saveToBin(const std::vector<std::complex<double>>& signal, const std::string& filename) {
    std::ofstream outFile(filename, std::ios::binary);
    
    if (!outFile) {
        Logger::getInstance().error("Error opening file for writing: " + filename + "\n");
        return;
    }

    for (const auto& sample : signal) {
        float re = static_cast<float>(sample.real());
        float im = static_cast<float>(sample.imag());
        
        outFile.write(reinterpret_cast<const char*>(&re), sizeof(float));
        outFile.write(reinterpret_cast<const char*>(&im), sizeof(float));
    }

    outFile.close();
    Logger::getInstance().info("Signal saved to " + filename + " (" + std::to_string(signal.size()) + " samples)");
}

std::vector<std::complex<double>> SignalFileIO::readFromBin(const std::string& filename) {
    std::ifstream inFile(filename, std::ios::binary);
    std::vector<std::complex<double>> signal;

    if (!inFile) {
        Logger::getInstance().error("Error opening file for reading: " + filename);
        return signal;
    }

    float re, im;
    while (inFile.read(reinterpret_cast<char*>(&re), sizeof(float)) &&
           inFile.read(reinterpret_cast<char*>(&im), sizeof(float))) {
        signal.push_back(std::complex<double>(static_cast<double>(re), static_cast<double>(im)));
    }

    inFile.close();
    Logger::getInstance().info("Signal loaded from " + filename + " (" + std::to_string(signal.size()) + " samples)");
    return signal;
}
