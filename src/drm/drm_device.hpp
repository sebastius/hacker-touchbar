#pragma once
#include <cstdint>
#include <string>
#include <xf86drmMode.h>

class DrmDevice {
public:
    DrmDevice();
    ~DrmDevice();

    bool open();
    void close();

    int fd() const { return fd_; }
    uint32_t connectorId() const { return connector_id_; }
    uint32_t crtcId() const { return crtc_id_; }

    // Logical dimensions, derived from the DRM mode at runtime
    // (long x short side, regardless of hardware rotation)
    int width() const { return rotated_ ? hw_height_ : hw_width_; }
    int height() const { return rotated_ ? hw_width_ : hw_height_; }

    // Hardware dimensions (may be 60x2170 if rotated)
    int hwWidth() const { return hw_width_; }
    int hwHeight() const { return hw_height_; }
    bool isRotated() const { return rotated_; }

    uint32_t hwPitch() const { return hw_pitch_; }
    const drmModeModeInfo& mode() const { return mode_; }

private:
    bool tryOpen(const char* path);

    int fd_;
    uint32_t connector_id_;
    uint32_t crtc_id_;
    int hw_width_;
    int hw_height_;
    uint32_t hw_pitch_;
    bool rotated_;
    drmModeModeInfo mode_;
    std::string device_path_;
};
