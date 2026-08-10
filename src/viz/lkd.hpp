#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Decoded Pioneer `.lkd` OEL clip: `frames` holds one luminance plane
// (0..255, row 0 = top) per animation frame, each width*height bytes.
struct LkdClip {
    int width = 0;
    int height = 0;
    std::string name;  // file stem, e.g. "movie8_f"
    std::vector<std::vector<uint8_t>> frames;
};

// Decode one clip. Format (reverse-engineered from carrozzeria head
// unit data): 20-byte header ("zLKD", version, 2 fields, frame count),
// then gzip -> tar -> single 24-bit bottom-up BMP of vertically
// stacked frames. Throws std::runtime_error on malformed data.
LkdClip loadLkd(const std::string& path);

// Decode every *.lkd in `dir` (sorted by filename). Files that fail
// to decode are skipped. Returns an empty vector when the directory
// does not exist or holds no usable clips.
std::vector<LkdClip> loadLkdDir(const std::string& dir);
