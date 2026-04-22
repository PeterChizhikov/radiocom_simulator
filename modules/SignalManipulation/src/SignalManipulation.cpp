#include "SignalManipulation.h"


void SignalManipulation::startEncodingManipulation(char* info){
    std::vector<uint8_t> encodedSignal = encodeWithHamming(info);
    encodedSignal[3] = 4; 




    decodeWithHamming(encodedSignal);
}

void SignalManipulation::startDecodingManipulation(){
  
}


std::vector<uint8_t> SignalManipulation::encodeWithHamming(char* info) {
    size_t actualSize = strlen(info) + 1; 
    
    size_t elen = Hamming4::encodedSize(actualSize);

    std::vector<uint8_t> p(elen);

    // Передаем указатель на данные вектора (p.data()) в функцию кодирования
    Hamming4::encode(p.data(), reinterpret_cast<uint8_t*>(info), actualSize);

    // Перемешивание
    Hamming4::mix8(p.data(), elen);

    // // ======== ИМИТАЦИЯ ОШИБКИ ========
    // if (elen > 3) {
    //     p[3] ^= 0xFF; // Инвертируем байт для теста
    // }

    return p; // Возвращаем объект вектора
}


void SignalManipulation::decodeWithHamming(std::vector<uint8_t> signal){
    size_t elen = signal.size();

    // 1. Размешивание (передаем указатель на начало данных)
    Hamming4::unmix8(signal.data(), elen);

    // 2. Размер распакованных данных
    size_t dlen = Hamming4::decodedSize(elen);
    std::cout << "Expected decode len: " << dlen << std::endl;

    // 3. Распаковка (обычно Hamming4::decode принимает указатель и размер)
    // Если библиотека декодирует прямо в тот же буфер:
    bool res = Hamming4::decode(signal.data(), elen);

    if (res) {
        // Используем string_view, ограничивая длину dlen, 
        // так как вектор signal всё еще имеет размер elen
        std::cout << "Decoded: " << std::string_view(reinterpret_cast<char*>(signal.data()), dlen) << std::endl;
    } else {
        std::cerr << "Error: Could not decode (too many errors)" << std::endl;
        // throw std::runtime_error("Hamming decode failed"); 
    }
}