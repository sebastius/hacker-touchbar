#pragma once
#include <cstdint>

class Framebuffer;

struct BitmapFont {
    int glyph_width;
    int glyph_height;
    int bytes_per_row;  // ceil(glyph_width / 8)
    const uint8_t* data;  // 95 glyphs (ASCII 32-126), each glyph_height * bytes_per_row bytes

    void drawChar(Framebuffer& fb, int x, int y, char c, uint32_t color) const;
};

const BitmapFont& font_normal();  // 8x16
const BitmapFont& font_small();   // 6x10
