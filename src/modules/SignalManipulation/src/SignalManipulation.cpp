#include "SignalManipulation.h"

std::vector<uint8_t> SignalManipulation::encodeWithHamming(char* info) {
    size_t actualSize = strlen(info) + 1; 
    std::cout << "actualInformationSize(number of verbs): " << actualSize << std::endl;
    printBits(reinterpret_cast<uint8_t*>(info), actualSize, "Original data (binary)");
    size_t signalCodeSize = Hamming4::encodedSize(actualSize);
    std::cout << "signalCodeSize(In bytes): " << signalCodeSize << std::endl;
    std::vector<uint8_t> p(signalCodeSize);

    Hamming4::encode(p.data(), reinterpret_cast<uint8_t*>(info), actualSize);
    printBits(p.data(), signalCodeSize, "After Hamming encode (before mix)");
    
    //Hamming4::mix8(p.data(), signalCodeSize);
    //printBits(p.data(), signalCodeSize, "After mix8 (final)");

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
//TODO (Petr 25.04.2026) Вынести в отедльный модуль запись в файл

void SignalManipulation::saveSignalToBin(const std::vector<std::complex<double>>& signal, const std::string& filename) {
    std::ofstream outFile(filename, std::ios::binary);
    
    if (!outFile) {
        std::cerr << "Error opening file for writing!" << std::endl;
        return;
    }

    for (const auto& sample : signal) {
        // GNU Radio ожидает float (32 бита), а у нас double (64 бита)
        // Приводим типы для совместимости
        float re = static_cast<float>(sample.real());
        float im = static_cast<float>(sample.imag());
        
        outFile.write(reinterpret_cast<const char*>(&re), sizeof(float));
        outFile.write(reinterpret_cast<const char*>(&im), sizeof(float));
    }

    outFile.close();
    std::cout << "Signal saved to " << filename << " (" << signal.size() << " samples)" << std::endl;
}

void SignalManipulation::startEncodingManipulation(char* info){
    std::vector<uint8_t> encodedSignal = encodeWithHamming(info);
    auto qpskSymbols = mapToQPSK(encodedSignal);

    std::cout << "qpskSymbols.size: " << qpskSymbols.size() << std::endl;

    auto mappingToSubcarriers = mapToSubcarriers(qpskSymbols);
    auto ofdmsybmols = createOfdmSymbol(mappingToSubcarriers);
    saveSignalToBin(ofdmsybmols,"test");
    decodeWithHamming(encodedSignal);
}

void SignalManipulation::startDecodingManipulation(){
  
}

void SignalManipulation::printBits(const uint8_t* data, size_t size, const std::string& prefix) {
    std::cout << prefix << " (" << size << " bytes): ";
    for (size_t i = 0; i < size; i++) {
        for (int bit = 7; bit >= 0; bit--) {  
            std::cout << ((data[i] >> bit) & 1);
        }
        if (i < size - 1) std::cout << " ";  // разделитель 
    }
    std::cout << std::endl;
}

void SignalManipulation::decodeWithHamming(std::vector<uint8_t> signal){
    size_t signalCodeSize = signal.size();

    //Hamming4::unmix8(signal.data(), signalCodeSize);

    size_t dlen = Hamming4::decodedSize(signalCodeSize);
    std::cout << "Expected decode len: " << dlen << std::endl;

    bool res = Hamming4::decode(signal.data(), signalCodeSize);

    if (res) {
        std::cout << "Decoded: " << std::string_view(reinterpret_cast<char*>(signal.data()), dlen) << std::endl;
    } else {
        std::cerr << "Error: Could not decode (too many errors)" << std::endl;
    }
}


