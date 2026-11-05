#pragma once
#include <cstdint>
#include <vector>
#include <string>

enum class FontSize { Small, Normal };

class Framebuffer {
public:
    Framebuffer(int width, int height);

    // Terminal themes: round rects draw square, for every widget at once.
    static bool square_corners;

    void clear(uint32_t color = 0xFF000000);
    void setPixel(int x, int y, uint32_t color);
    uint32_t getPixel(int x, int y) const;

    void fillRect(int x, int y, int w, int h, uint32_t color);
    void drawRect(int x, int y, int w, int h, uint32_t color);
    void drawHLine(int x, int y, int w, uint32_t color);
    void drawVLine(int x, int y, int h, uint32_t color);
    void drawLine(int x1, int y1, int x2, int y2, uint32_t color);
    void fillCircle(int cx, int cy, int r, uint32_t color);
    void fillRoundRect(int x, int y, int w, int h, int r, uint32_t color);
    void drawRoundRect(int x, int y, int w, int h, int r, uint32_t color);

    void drawText(int x, int y, const std::string& text, uint32_t color,
                  FontSize size = FontSize::Normal);
    int textWidth(const std::string& text, FontSize size = FontSize::Normal) const;

    // Blit a sub-region from src into this framebuffer
    void blit(const Framebuffer& src, int sx, int sy, int w, int h, int dx, int dy);

    int width() const { return w_; }
    int height() const { return h_; }
    const uint32_t* data() const { return pixels_.data(); }
    uint32_t* data() { return pixels_.data(); }

private:
    int w_, h_;
    std::vector<uint32_t> pixels_;
};
