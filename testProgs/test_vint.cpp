#include <stdio.h>
#include <stdint.h>

// Test VINT encoding/decoding
void encodeVINT(uint64_t value, uint8_t* buffer, int* length) {
    if (value <= 126) {
        buffer[0] = 0x80 | (uint8_t)value;
        *length = 1;
    } else if (value <= 16382) {
        buffer[0] = 0x40 | (uint8_t)(value >> 8);
        buffer[1] = (uint8_t)value;
        *length = 2;
    } else if (value <= 2097150) {
        buffer[0] = 0x20 | (uint8_t)(value >> 16);
        buffer[1] = (uint8_t)(value >> 8);
        buffer[2] = (uint8_t)value;
        *length = 3;
    } else {
        printf("Value too large for this test\n");
        *length = 0;
    }
}

uint64_t decodeVINT(uint8_t* buffer) {
    if (buffer[0] & 0x80) {
        // 1-byte VINT
        return buffer[0] & 0x7F;
    } else if (buffer[0] & 0x40) {
        // 2-byte VINT
        return ((buffer[0] & 0x3F) << 8) | buffer[1];
    } else if (buffer[0] & 0x20) {
        // 3-byte VINT
        return ((buffer[0] & 0x1F) << 16) | (buffer[1] << 8) | buffer[2];
    }
    return 0;
}

int main() {
    uint64_t testValues[] = {203, 126, 127, 16382, 16383};
    int numTests = sizeof(testValues) / sizeof(testValues[0]);

    for (int i = 0; i < numTests; i++) {
        uint64_t value = testValues[i];
        uint8_t buffer[8];
        int length;

        encodeVINT(value, buffer, &length);
        uint64_t decoded = decodeVINT(buffer);

        printf("Value: %llu, Encoded: ", (unsigned long long)value);
        for (int j = 0; j < length; j++) {
            printf("0x%02X ", buffer[j]);
        }
        printf("Decoded: %llu %s\n", (unsigned long long)decoded,
               (decoded == value) ? "✓" : "✗");
    }

    return 0;
}
