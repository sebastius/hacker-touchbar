#include "output/dump_output.hpp"
#include "gfx/framebuffer.hpp"
#include "util/log.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

DumpOutput::DumpOutput(const std::string& dir) : dir_(dir) {
    // mkdir -p
    std::string path;
    for (size_t i = 0; i <= dir_.size(); ++i) {
        if (i == dir_.size() || dir_[i] == '/') {
            if (!path.empty() && mkdir(path.c_str(), 0755) < 0 && errno != EEXIST)
                log_warn("DumpOutput: cannot create %s: %s", path.c_str(), strerror(errno));
        }
        if (i < dir_.size()) path += dir_[i];
    }
}

void DumpOutput::present(const Framebuffer& fb) {
    char name[64];
    snprintf(name, sizeof(name), "frame_%04d.bmp", ++count_);
    std::string path = dir_;
    if (!path.empty() && path.back() != '/') path += '/';
    path += name;
    writeBmp(fb, path);
}

bool DumpOutput::writeBmp(const Framebuffer& fb, const std::string& path) {
    const int w = fb.width();
    const int h = fb.height();
    const int row_size = (w * 3 + 3) & ~3;
    const uint32_t img_size = row_size * h;
    const uint32_t file_size = 54 + img_size;

    uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M';
    std::memcpy(header + 2, &file_size, 4);
    uint32_t offset = 54;
    std::memcpy(header + 10, &offset, 4);
    uint32_t dib = 40;
    std::memcpy(header + 14, &dib, 4);
    std::memcpy(header + 18, &w, 4);
    std::memcpy(header + 22, &h, 4);  // positive = bottom-up
    uint16_t planes = 1, bpp = 24;
    std::memcpy(header + 26, &planes, 2);
    std::memcpy(header + 28, &bpp, 2);
    std::memcpy(header + 34, &img_size, 4);

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        log_error("DumpOutput: cannot write %s: %s", path.c_str(), strerror(errno));
        return false;
    }
    fwrite(header, 1, sizeof(header), f);

    std::vector<uint8_t> row(row_size, 0);
    for (int y = h - 1; y >= 0; --y) {
        const uint32_t* src = fb.data() + static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            row[x * 3 + 0] = src[x] & 0xFF;          // B
            row[x * 3 + 1] = (src[x] >> 8) & 0xFF;   // G
            row[x * 3 + 2] = (src[x] >> 16) & 0xFF;  // R
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
    return true;
}
