#include "gfx/framebuffer.hpp"
#include "gfx/font.hpp"
#include <algorithm>
#include <cmath>

Framebuffer::Framebuffer(int width, int height)
    : w_(width), h_(height), pixels_(width * height, 0xFF000000u)
{}

void Framebuffer::clear(uint32_t color) {
    std::fill(pixels_.begin(), pixels_.end(), color);
}

void Framebuffer::setPixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < w_ && y >= 0 && y < h_)
        pixels_[y * w_ + x] = color;
}

uint32_t Framebuffer::getPixel(int x, int y) const {
    if (x >= 0 && x < w_ && y >= 0 && y < h_)
        return pixels_[y * w_ + x];
    return 0;
}

void Framebuffer::fillRect(int x, int y, int w, int h, uint32_t color) {
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(w_, x + w);
    int y1 = std::min(h_, y + h);
    for (int row = y0; row < y1; ++row)
        for (int col = x0; col < x1; ++col)
            pixels_[row * w_ + col] = color;
}

void Framebuffer::drawRect(int x, int y, int w, int h, uint32_t color) {
    drawHLine(x, y, w, color);
    drawHLine(x, y + h - 1, w, color);
    drawVLine(x, y, h, color);
    drawVLine(x + w - 1, y, h, color);
}

void Framebuffer::drawHLine(int x, int y, int w, uint32_t color) {
    if (y < 0 || y >= h_) return;
    int x0 = std::max(0, x);
    int x1 = std::min(w_, x + w);
    for (int i = x0; i < x1; ++i)
        pixels_[y * w_ + i] = color;
}

void Framebuffer::drawVLine(int x, int y, int h, uint32_t color) {
    if (x < 0 || x >= w_) return;
    int y0 = std::max(0, y);
    int y1 = std::min(h_, y + h);
    for (int i = y0; i < y1; ++i)
        pixels_[i * w_ + x] = color;
}

void Framebuffer::drawLine(int x1, int y1, int x2, int y2, uint32_t color) {
    // Bresenham
    int dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        setPixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void Framebuffer::fillCircle(int cx, int cy, int r, uint32_t color) {
    for (int dy = -r; dy <= r; ++dy) {
        int dx = static_cast<int>(std::sqrt(r * r - dy * dy));
        drawHLine(cx - dx, cy + dy, 2 * dx + 1, color);
    }
}

bool Framebuffer::square_corners = false;

void Framebuffer::fillRoundRect(int x, int y, int w, int h, int r, uint32_t color) {
    if (square_corners) { fillRect(x, y, w, h, color); return; }
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    // Center rectangle
    fillRect(x + r, y, w - 2 * r, h, color);
    // Left and right rectangles
    fillRect(x, y + r, r, h - 2 * r, color);
    fillRect(x + w - r, y + r, r, h - 2 * r, color);
    // Four corner circles
    for (int dy = -r; dy <= 0; ++dy) {
        int dx = static_cast<int>(std::sqrt(r * r - dy * dy));
        drawHLine(x + r - dx, y + r + dy, dx, color);           // top-left
        drawHLine(x + w - r, y + r + dy, dx, color);             // top-right
        drawHLine(x + r - dx, y + h - 1 - r - dy, dx, color);   // bottom-left
        drawHLine(x + w - r, y + h - 1 - r - dy, dx, color);    // bottom-right
    }
}

void Framebuffer::drawRoundRect(int x, int y, int w, int h, int r, uint32_t color) {
    if (square_corners) { drawRect(x, y, w, h, color); return; }
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    // Top and bottom edges
    drawHLine(x + r, y, w - 2 * r, color);
    drawHLine(x + r, y + h - 1, w - 2 * r, color);
    // Left and right edges
    drawVLine(x, y + r, h - 2 * r, color);
    drawVLine(x + w - 1, y + r, h - 2 * r, color);
    // Corner arcs
    for (int a = 0; a <= r; ++a) {
        int b = static_cast<int>(std::sqrt(r * r - a * a));
        setPixel(x + r - a, y + r - b, color);         // top-left
        setPixel(x + w - 1 - r + a, y + r - b, color); // top-right
        setPixel(x + r - a, y + h - 1 - r + b, color); // bottom-left
        setPixel(x + w - 1 - r + a, y + h - 1 - r + b, color); // bottom-right
    }
}

void Framebuffer::drawText(int x, int y, const std::string& text, uint32_t color,
                           FontSize size) {
    const BitmapFont& font = (size == FontSize::Small) ? font_small() : font_normal();
    int cx = x;
    for (char c : text) {
        font.drawChar(*this, cx, y, c, color);
        cx += font.glyph_width;
    }
}

int Framebuffer::textWidth(const std::string& text, FontSize size) const {
    const BitmapFont& font = (size == FontSize::Small) ? font_small() : font_normal();
    return static_cast<int>(text.size()) * font.glyph_width;
}

void Framebuffer::blit(const Framebuffer& src, int sx, int sy, int w, int h, int dx, int dy) {
    for (int row = 0; row < h; ++row) {
        int sr = sy + row, dr = dy + row;
        if (sr < 0 || sr >= src.h_ || dr < 0 || dr >= h_) continue;
        for (int col = 0; col < w; ++col) {
            int sc = sx + col, dc = dx + col;
            if (sc < 0 || sc >= src.w_ || dc < 0 || dc >= w_) continue;
            pixels_[dr * w_ + dc] = src.pixels_[sr * src.w_ + sc];
        }
    }
}
