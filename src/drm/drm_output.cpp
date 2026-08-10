#include "drm/drm_output.hpp"
#include "gfx/framebuffer.hpp"
#include "util/log.hpp"
#include <sys/mman.h>
#include <cstring>
#include <cerrno>
#include <xf86drm.h>
#include <xf86drmMode.h>

DrmOutput::DrmOutput(DrmDevice& dev) : dev_(dev) {}

DrmOutput::~DrmOutput() {
    shutdown();
}

bool DrmOutput::init() {
    for (int i = 0; i < 2; ++i) {
        if (!createBuffer(i)) {
            shutdown();
            return false;
        }
    }
    log_info("DRM output initialized (double-buffered)");
    return true;
}

void DrmOutput::present(const Framebuffer& fb) {
    Buffer& buf = bufs_[back_];

    if (dev_.isRotated()) {
        // Transpose: logical 2170x60 -> hardware 60x2170
        // Logical pixel (x, y) maps to hardware pixel (y, x)
        auto* dst = static_cast<uint32_t*>(buf.map);
        const uint32_t* src = fb.data();
        int lw = fb.width();   // 2170
        int lh = fb.height();  // 60
        int hw = dev_.hwWidth();  // 60

        for (int ly = 0; ly < lh; ++ly) {
            for (int lx = 0; lx < lw; ++lx) {
                // 90-degree CW rotation: logical (lx, ly) -> hardware (lh-1-ly, lx)
                dst[lx * hw + (lh - 1 - ly)] = src[ly * lw + lx];
            }
        }
    } else {
        // Direct copy
        size_t copy_size = fb.width() * fb.height() * sizeof(uint32_t);
        if (copy_size > buf.size) copy_size = buf.size;
        memcpy(buf.map, fb.data(), copy_size);
    }

    uint32_t conn_id = dev_.connectorId();
    drmModeModeInfo mode = dev_.mode();
    drmModeSetCrtc(dev_.fd(), dev_.crtcId(), buf.fb_id, 0, 0,
                   &conn_id, 1, &mode);

    back_ ^= 1;
}

void DrmOutput::shutdown() {
    for (int i = 0; i < 2; ++i) destroyBuffer(i);
}

bool DrmOutput::createBuffer(int index) {
    struct drm_mode_create_dumb req = {};
    req.width = dev_.hwWidth();
    req.height = dev_.hwHeight();
    req.bpp = 32;

    if (drmIoctl(dev_.fd(), DRM_IOCTL_MODE_CREATE_DUMB, &req) < 0) {
        log_error("Failed to create dumb buffer %d: %s", index, strerror(errno));
        return false;
    }

    Buffer& buf = bufs_[index];
    buf.handle = req.handle;
    buf.pitch = req.pitch;
    buf.size = req.size;

    // Map to userspace
    struct drm_mode_map_dumb map_req = {};
    map_req.handle = buf.handle;
    if (drmIoctl(dev_.fd(), DRM_IOCTL_MODE_MAP_DUMB, &map_req) < 0) {
        log_error("Failed to map dumb buffer: %s", strerror(errno));
        destroyBuffer(index);
        return false;
    }

    buf.map = mmap(nullptr, buf.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   dev_.fd(), map_req.offset);
    if (buf.map == MAP_FAILED) {
        log_error("mmap failed: %s", strerror(errno));
        buf.map = nullptr;
        destroyBuffer(index);
        return false;
    }

    // Register as DRM framebuffer
    if (drmModeAddFB(dev_.fd(), dev_.hwWidth(), dev_.hwHeight(),
                     24, 32, buf.pitch, buf.handle, &buf.fb_id) != 0) {
        log_error("drmModeAddFB failed: %s", strerror(errno));
        destroyBuffer(index);
        return false;
    }

    return true;
}

void DrmOutput::destroyBuffer(int index) {
    Buffer& buf = bufs_[index];
    if (buf.fb_id) {
        drmModeRmFB(dev_.fd(), buf.fb_id);
        buf.fb_id = 0;
    }
    if (buf.map) {
        munmap(buf.map, buf.size);
        buf.map = nullptr;
    }
    if (buf.handle) {
        struct drm_mode_destroy_dumb req = {};
        req.handle = buf.handle;
        drmIoctl(dev_.fd(), DRM_IOCTL_MODE_DESTROY_DUMB, &req);
        buf.handle = 0;
    }
}
