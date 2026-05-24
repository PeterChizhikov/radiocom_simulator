#include "SignalManipulation.h"

SignalManipulation::SignalManipulation(std::string codeType)  : codeType(codeType) {
}

std::vector<uint8_t> SignalManipulation::encodeWithHamming(char* info) {
    size_t actualSize = strlen(info) + 1; 
    Logger::getInstance().info("actualInformationSize(number of verbs): "+std::to_string(actualSize));
    //printBits(reinterpret_cast<uint8_t*>(info), actualSize, "Original data (binary)");
    size_t signalCodeSize = Hamming4::encodedSize(actualSize);
    Logger::getInstance().info("signalCodeSize(In bytes): "+std::to_string(signalCodeSize));
    std::vector<uint8_t> p(signalCodeSize);
    Hamming4::encode(p.data(), reinterpret_cast<uint8_t*>(info), actualSize);
    originalBytes = p;
    //printBits(p.data(), signalCodeSize, "After Hamming encode (before mix)");
    
    return p; 
}

std::vector<QPSKSymbol> SignalManipulation::mapToQPSK(const std::vector<uint8_t>& bytes){
    std::vector<QPSKSymbol> symbols;

    for (uint8_t simpleByte : bytes) {
        for (int i = 0; i < 8; i += 2) {

            uint8_t pair = (simpleByte >> i) & 0b11;
            
            double I, Q;
            switch (pair) {
                case 0b00: I =  0.707; Q =  0.707; break;
                case 0b01: I = -0.707; Q =  0.707; break;
                case 0b11: I = -0.707; Q = -0.707; break;
                case 0b10: I =  0.707; Q = -0.707; break;
            }
            symbols.push_back({std::complex<double>(I, Q)});
        }
    }
    return symbols;
}

std::vector<std::complex<double>> SignalManipulation::mapToSubcarriers(const std::vector<QPSKSymbol>& qpskSymbols) {
    const int FFT_SIZE = 256;

    std::vector<std::complex<double>> fftBins(FFT_SIZE, std::complex<double>(0.0, 0.0));

    int halfSize = static_cast<int>(qpskSymbols.size()) / 2;
    if (halfSize > 64) halfSize = 64; 

    for (int i = 0; i < halfSize; ++i) {
        fftBins[1 + i] = qpskSymbols[i].value;
    }

    for (int i = 0; i < halfSize; ++i) {
        fftBins[FFT_SIZE - halfSize + i] = qpskSymbols[halfSize + i].value;
    }
    
    return fftBins;
}

std::vector<std::complex<double>> SignalManipulation::createOfdmSymbol(const std::vector<std::complex<double>>& fftBins) {
    const int FFT_SIZE = 256;
    const int CP_SIZE = 18; 
    
    std::vector<std::complex<double>> timeDomain(FFT_SIZE);
    
    fftw_plan plan = fftw_plan_dft_1d(FFT_SIZE, 
        reinterpret_cast<fftw_complex*>(const_cast<std::complex<double>*>(fftBins.data())), 
        reinterpret_cast<fftw_complex*>(timeDomain.data()), 
        FFTW_BACKWARD, FFTW_ESTIMATE);

    fftw_execute(plan);

    for (int i = 0; i < FFT_SIZE; ++i) {
        timeDomain[i] /= static_cast<double>(FFT_SIZE);
    }

    std::vector<std::complex<double>> ofdmSymbolWithCP;
    ofdmSymbolWithCP.reserve(FFT_SIZE + CP_SIZE);

    for (int i = FFT_SIZE - CP_SIZE; i < FFT_SIZE; ++i) {
        ofdmSymbolWithCP.push_back(timeDomain[i]);
    }

    for (int i = 0; i < FFT_SIZE; ++i) {
        ofdmSymbolWithCP.push_back(timeDomain[i]);
    }

    fftw_destroy_plan(plan);

    return ofdmSymbolWithCP;
}

std::vector<std::complex<double>> SignalManipulation::addAWGN(const std::vector<std::complex<double>>& signal, double snr_db) {
    double signal_power = 0.0;
    for (const auto& s : signal) {
        signal_power += std::norm(s);
    }
    signal_power /= signal.size();

    double snr_linear = std::pow(10.0, snr_db / 10.0);
    double noise_power = signal_power / snr_linear;
    double noise_std = std::sqrt(noise_power / 2.0); 

    static std::mt19937 gen(std::random_device{}());
    std::normal_distribution<double> dist(0.0, noise_std);

    std::vector<std::complex<double>> noisy_signal = signal;
    for (auto& sample : noisy_signal) {
        sample += std::complex<double>(dist(gen), dist(gen));
    }
    return noisy_signal;
}

void SignalManipulation::startEncodingManipulation(char* info, double snr){
    std::vector<uint8_t> encodedSignal = encodeWithHamming(info);
    auto qpskSymbols = mapToQPSK(encodedSignal);

    Logger::getInstance().info("qpskSymbols.size: "+ std::to_string(qpskSymbols.size()));

    auto mappingToSubcarriers = mapToSubcarriers(qpskSymbols);
    std::vector<std::complex<double>> ofdmsybmols = createOfdmSymbol(mappingToSubcarriers);

    auto noisySignal = addAWGN(ofdmsybmols, snr);

    SignalFileIO::saveToBin(noisySignal, "test");
}

std::vector<std::complex<double>> SignalManipulation::demodulateOfdmSymbol(const std::vector<std::complex<double>>& ofdmSymbolWithCP) {
    const int FFT_SIZE = 256;
    const int CP_SIZE = 18;

    if (ofdmSymbolWithCP.size() < static_cast<size_t>(FFT_SIZE + CP_SIZE)) {
        Logger::getInstance().error("Error: Signal is too short to extract OFDM symbol!");
        return std::vector<std::complex<double>>(FFT_SIZE, std::complex<double>(0.0, 0.0));
    }

    std::vector<std::complex<double>> timeDomain(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; ++i) {
        timeDomain[i] = ofdmSymbolWithCP[CP_SIZE + i];
    }

    std::vector<std::complex<double>> fftBins(FFT_SIZE);

    fftw_plan plan = fftw_plan_dft_1d(FFT_SIZE,
        reinterpret_cast<fftw_complex*>(timeDomain.data()),
        reinterpret_cast<fftw_complex*>(fftBins.data()),
        FFTW_FORWARD, FFTW_ESTIMATE);

    fftw_execute(plan);
    fftw_destroy_plan(plan);

    return fftBins;
}

std::vector<QPSKSymbol> SignalManipulation::demapFromSubcarriers(const std::vector<std::complex<double>>& fftBins, size_t expectedSymbolsCount) {
    std::vector<QPSKSymbol> qpskSymbols;
    qpskSymbols.reserve(expectedSymbolsCount);

    int halfSize = static_cast<int>(expectedSymbolsCount) / 2;
    if (halfSize > 64) halfSize = 64;

    for (int i = 0; i < halfSize; ++i) {
        qpskSymbols.push_back({fftBins[1 + i]});
    }

    for (int i = 0; i < halfSize; ++i) {
        qpskSymbols.push_back({fftBins[256 - halfSize + i]});
    }

    return qpskSymbols;
}

std::vector<uint8_t> SignalManipulation::demapFromQPSK(const std::vector<QPSKSymbol>& qpskSymbols) {
    std::vector<uint8_t> bytes;
    bytes.reserve(qpskSymbols.size() / 4); 

    uint8_t currentByte = 0;
    int bitShift = 0;

    for (const auto& symbol : qpskSymbols) {
        double I = symbol.value.real();
        double Q = symbol.value.imag();
        uint8_t pair = 0b00;

        if (I >= 0 && Q >= 0)       pair = 0b00; 
        else if (I < 0 && Q >= 0)  pair = 0b01; 
        else if (I < 0 && Q < 0)   pair = 0b11; 
        else if (I >= 0 && Q < 0)  pair = 0b10; 

        currentByte |= (pair << bitShift);
        bitShift += 2;

        if (bitShift == 8) {
            bytes.push_back(currentByte);
            currentByte = 0;
            bitShift = 0;
        }
    }
    return bytes;
}

double SignalManipulation::calculateBER(const std::vector<uint8_t>& original, const std::vector<uint8_t>& received)
{
    if (original.size() != received.size()) {
        throw std::runtime_error("BER size mismatch");
    }

    size_t errors = 0;

    for (size_t i = 0; i < original.size(); ++i) {
        errors += __builtin_popcount(
            original[i] ^ received[i]
        );
    }

    return static_cast<double>(errors) /
           (original.size() * 8);
}

void SignalManipulation::startDecodingManipulation(bool &resultStatus, double &BER){
    auto loadedSignal = SignalFileIO::readFromBin("test");
    if (loadedSignal.empty()) {
        Logger::getInstance().error(" Decoding aborted: file is empty or missing. ");
        return;
    }

    auto fftBins = demodulateOfdmSymbol(loadedSignal);

    size_t expectedQpskCount = 128; 
    auto recoveredQpsk = demapFromSubcarriers(fftBins, expectedQpskCount);
    Logger::getInstance().info("Recovered QPSK symbols count: "+ std::to_string(recoveredQpsk.size()));

    auto encodedBytes = demapFromQPSK(recoveredQpsk);
    size_t dlen = Hamming4::decodedSize(encodedBytes.size());
    Logger::getInstance().info("Expected original text length: "+ std::to_string(dlen) + " bytes");

    // BER сравниваем originalBytes (сохранённые при кодировании) с encodedBytes
    BER = calculateBER(originalBytes, encodedBytes);
    bool success = Hamming4::decode(encodedBytes.data(), encodedBytes.size());
    if (success) {
        Logger::getInstance().info("Decoded text: " + 
            std::string(reinterpret_cast<char*>(encodedBytes.data()), dlen));
    }
    resultStatus = success;
}

void SignalManipulation::printBits(const uint8_t* data, size_t size, const std::string& prefix) {
    Logger::getInstance().info(prefix+ " (" + std::to_string(size) + " bytes):");
    for (size_t i = 0; i < size; i++) {
        for (int bit = 7; bit >= 0; bit--) {  
            Logger::getInstance().info(std::to_string(((data[i] >> bit) & 1)));
        }
        if (i < size - 1) Logger::getInstance().info(" ");  // разделитель 
    }
    Logger::getInstance().info("\n");
}

void SignalManipulation::decodeWithHamming(std::vector<uint8_t> signal){
    size_t signalCodeSize = signal.size();

    size_t dlen = Hamming4::decodedSize(signalCodeSize);
    Logger::getInstance().info("Expected decode len: " + std::to_string(dlen));

    bool res = Hamming4::decode(signal.data(), signalCodeSize);

    if (res) {
        Logger::getInstance().info("Decoded: " + std::string(reinterpret_cast<char*>(signal.data()), dlen) + "\n ");
    } else {
        Logger::getInstance().error("Error: Could not decode (too many errors) ");
    }
}


