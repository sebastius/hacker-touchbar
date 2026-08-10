#pragma once
#include <cstdint>
#include <cmath>

inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

inline uint32_t hsv_to_rgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r1, g1, b1;
    if (h < 60)       { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
    else               { r1 = c; g1 = 0; b1 = x; }
    return rgb(
        static_cast<uint8_t>((r1 + m) * 255),
        static_cast<uint8_t>((g1 + m) * 255),
        static_cast<uint8_t>((b1 + m) * 255)
    );
}

inline uint32_t hex_to_color(const char* s) {
    if (s[0] == '#') ++s;
    unsigned long v = 0;
    for (int i = 0; i < 6 && s[i]; ++i) {
        v <<= 4;
        char c = s[i];
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') v |= 10 + c - 'A';
    }
    return 0xFF000000u | (v & 0xFFFFFF);
}
