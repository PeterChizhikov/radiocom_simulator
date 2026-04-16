#include "SignalManipulation.h"


void SignalManipulation::startEncodingManipulation(){
    encodeWithHamming(testCase);
}

void SignalManipulation::startDecodingManipulation(){
    decodeWithHamming();
}


void SignalManipulation::encodeWithHamming(char* info){
    size_t actualSize = strlen(info) + 1; 
    std::cout << "data len: " << actualSize << std::endl;

    // размер запакованных данных
    size_t elen = Hamming4::encodedSize(actualSize);

    std::cout << "pack len: " << sizeof(elen) << std::endl;

    // буфер для пакета
    uint8_t p[elen] = {};

    Hamming4::encode(p, info, actualSize);

    Hamming4::mix8(p, elen);

    for(int i =0 ; i < elen; i++){
        std::cout << "Elen: " << p[i] << std::endl;
    }

    // ======== ПЕРЕДАЧА ========
    // имитация порчи данных

    p[3] = 0;  // байт
    p[3] = 1;  // байт
    p[3] = 2;  // байт
    // bitWrite(p[9], 3, !bitRead(p[9], 3));  // бит

    // ======== ПЕРЕДАЧА ========

    // размешивание
    Hamming4::unmix8(p, elen);
    // Hamming4::unmix(p, elen);

    // размер распакованных данных
    size_t dlen = Hamming4::decodedSize(sizeof(p));

    std::cout << "decode len: " << dlen << std::endl;

    // распаковка (в этот же буфер)
    bool res = Hamming4::decode(p, sizeof(p));

    if(res){
        std::cout << "Decoded: " << std::string_view((char*)p, dlen) << std::endl;
    }else{
        throw;
    }

}


void SignalManipulation::decodeWithHamming(){}