#include "viz/lkd.hpp"
#include <zlib.h>
#include <dirent.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        throw std::runtime_error("empty file " + path);
    }
    std::vector<uint8_t> data(static_cast<size_t>(size));
    size_t got = fread(data.data(), 1, data.size(), f);
    fclose(f);
    if (got != data.size())
        throw std::runtime_error("short read on " + path);
    return data;
}

uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

std::vector<uint8_t> gunzip(const uint8_t* src, size_t len) {
    z_stream zs{};
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)
        throw std::runtime_error("inflateInit2 failed");
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(len);

    std::vector<uint8_t> out;
    uint8_t chunk[65536];
    int ret;
    do {
        zs.next_out = chunk;
        zs.avail_out = sizeof(chunk);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            throw std::runtime_error("gzip inflate failed");
        }
        out.insert(out.end(), chunk, chunk + (sizeof(chunk) - zs.avail_out));
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
    return out;
}

// First member of a tar archive: name at 0 (100 bytes), octal size at
// 124 (12 bytes), data at offset 512, padded to 512.
std::vector<uint8_t> untarFirst(const std::vector<uint8_t>& tar) {
    if (tar.size() < 1024) throw std::runtime_error("tar payload too small");
    char size_str[13] = {};
    memcpy(size_str, tar.data() + 124, 12);
    const size_t size = static_cast<size_t>(strtoull(size_str, nullptr, 8));
    if (size == 0 || 512 + size > tar.size())
        throw std::runtime_error("bad tar member size");
    return std::vector<uint8_t>(tar.begin() + 512,
                                tar.begin() + 512 + size);
}

}  // namespace

LkdClip loadLkd(const std::string& path) {
    const std::vector<uint8_t> data = readFile(path);
    if (data.size() < 20 || memcmp(data.data(), "zLKD", 4) != 0)
        throw std::runtime_error(path + ": bad magic");
    int frame_count = static_cast<int>(le32(data.data() + 16));
    if (frame_count <= 0)
        throw std::runtime_error(path + ": bad frame count");

    const std::vector<uint8_t> bmp = untarFirst(gunzip(data.data() + 20,
                                                       data.size() - 20));
    if (bmp.size() < 54 || bmp[0] != 'B' || bmp[1] != 'M')
        throw std::runtime_error(path + ": bad BMP");
    const uint32_t data_off = le32(bmp.data() + 10);
    const int32_t w = static_cast<int32_t>(le32(bmp.data() + 18));
    const int32_t h = static_cast<int32_t>(le32(bmp.data() + 22));
    const uint16_t bpp = le16(bmp.data() + 28);
    if (w <= 0 || h <= 0 || bpp != 24)
        throw std::runtime_error(path + ": unsupported BMP geometry");
    if (h % frame_count != 0)
        throw std::runtime_error(path + ": strip height not divisible by frames");
    const int fh = h / frame_count;
    const size_t row_size = (static_cast<size_t>(w) * 3 + 3) & ~size_t(3);
    if (data_off + row_size * static_cast<size_t>(h) > bmp.size())
        throw std::runtime_error(path + ": truncated BMP");

    LkdClip clip;
    clip.width = w;
    clip.height = fh;
    // Stem without directory or extension.
    const size_t slash = path.find_last_of('/');
    const size_t dot = path.find_last_of('.');
    clip.name = path.substr(slash == std::string::npos ? 0 : slash + 1,
                            (dot == std::string::npos ? path.size() : dot) -
                                (slash == std::string::npos ? 0 : slash + 1));

    clip.frames.reserve(static_cast<size_t>(frame_count));
    for (int fi = 0; fi < frame_count; ++fi) {
        std::vector<uint8_t> frame(static_cast<size_t>(w) * fh);
        for (int y = 0; y < fh; ++y) {
            // BMP rows are bottom-up within the whole strip; frame 0
            // is the top chunk of the image, so its first row is the
            // last BMP row.
            const size_t src_row = static_cast<size_t>(h - 1 - (fi * fh + y));
            const uint8_t* src = bmp.data() + data_off + src_row * row_size;
            uint8_t* dst = frame.data() + static_cast<size_t>(y) * w;
            for (int x = 0; x < w; ++x) {
                const uint8_t b = src[x * 3 + 0];
                const uint8_t g = src[x * 3 + 1];
                const uint8_t r = src[x * 3 + 2];
                // OEL cyan clips: max() tracks phosphor intensity best.
                dst[x] = std::max({r, g, b});
            }
        }
        clip.frames.push_back(std::move(frame));
    }
    return clip;
}

std::vector<LkdClip> loadLkdDir(const std::string& dir) {
    std::vector<std::string> paths;
    if (DIR* d = opendir(dir.c_str())) {
        while (const dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".lkd") == 0)
                paths.push_back(dir + "/" + name);
        }
        closedir(d);
    }
    std::sort(paths.begin(), paths.end());

    std::vector<LkdClip> clips;
    for (const std::string& p : paths) {
        try {
            clips.push_back(loadLkd(p));
        } catch (const std::exception&) {
            // Skip unreadable clips; the preset reports if none load.
        }
    }
    return clips;
}
