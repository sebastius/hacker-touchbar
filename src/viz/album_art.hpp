#pragma once
#include <cstdint>
#include <vector>

// Latest album art thumbnail as ARGB pixels. Normally filled by the
// MPRIS art fetcher (media/art_fetcher.cpp) on each track change; the
// "art" IPC/HTTP command accepts the same thing as RGB565 hex for
// external pushes. Shared, mutable, single-threaded (event loop). The
// media center overlay draws it next to the track text when present.
struct AlbumArt {
    int w = 0, h = 0;
    std::vector<uint32_t> px;  // ARGB, row-major
    uint64_t seq = 0;          // bumps on each push
};

inline AlbumArt& albumArt() {
    static AlbumArt art;
    return art;
}

inline uint32_t rgb565_to_argb(uint16_t v) {
    const unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    return 0xFF000000u | ((r * 255 / 31) << 16) | ((g * 255 / 63) << 8)
           | (b * 255 / 31);
}
