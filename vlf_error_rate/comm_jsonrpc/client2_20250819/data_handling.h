#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================
// Data Handling Functions
// ============================================================

inline char dtoc(int n) {
    if (n < 0 || n > 9) return '\0';  
    return '0' + n;
}

inline char binary_to_hex_char(const String &binary) {
    if (binary.length() != 4) return '\0';
    int value = 0;
    if (binary[0] == '1') value += 8;
    if (binary[1] == '1') value += 4;
    if (binary[2] == '1') value += 2;
    if (binary[3] == '1') value += 1;

    if (value < 10) {
        return '0' + value;
    } else {
        return 'A' + (value - 10);
    }
}

inline String binary_to_hex_string(const String &binary) {
    String hex = "";
    int len = binary.length();
    
    // 1024文字に制限（4の倍数になるようにする）
    int targetLen = min(len, BINARY_BUFFER_SIZE);
    // 4の倍数になるように調整
    targetLen = (targetLen / 4) * 4;

    hex.reserve(targetLen / 4);
    
    for (int i = 0; i < targetLen; i += 4) {
        String nibble = binary.substring(i, i + 4);
        
        char hexChar = binary_to_hex_char(nibble);
        if (hexChar != '\0') {
            hex += hexChar;
        }
    }
    return hex;
}
