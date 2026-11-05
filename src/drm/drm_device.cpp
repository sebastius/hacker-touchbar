#include "drm/drm_device.hpp"
#include "util/log.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <xf86drm.h>
#include <xf86drmMode.h>

DrmDevice::DrmDevice()
    : fd_(-1), connector_id_(0), crtc_id_(0),
      hw_width_(0), hw_height_(0), hw_pitch_(0),
      rotated_(false), mode_{}
{}

DrmDevice::~DrmDevice() {
    close();
}

bool DrmDevice::open() {
    const char* devices[] = {"/dev/dri/card0", "/dev/dri/card1", "/dev/dri/card2", nullptr};
    for (int i = 0; devices[i]; ++i) {
        if (tryOpen(devices[i])) {
            log_info("Touch Bar found on %s (%dx%d%s)",
                     device_path_.c_str(), hw_width_, hw_height_,
                     rotated_ ? ", rotated" : "");
            return true;
        }
    }
    log_error("No Touch Bar DRM device found");
    return false;
}

void DrmDevice::close() {
    if (fd_ >= 0) {
        drmDropMaster(fd_);
        ::close(fd_);
        fd_ = -1;
    }
}

bool DrmDevice::tryOpen(const char* path) {
    int fd = ::open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;

    // Try to become DRM master
    if (drmSetMaster(fd) < 0) {
        log_warn("Could not become DRM master on %s: %s", path, strerror(errno));
        ::close(fd);
        return false;
    }

    drmModeRes* res = drmModeGetResources(fd);
    if (!res) {
        drmDropMaster(fd);
        ::close(fd);
        return false;
    }

    bool found = false;
    for (int i = 0; i < res->count_connectors && !found; ++i) {
        drmModeConnector* conn = drmModeGetConnector(fd, res->connectors[i]);
        if (!conn) continue;

        if (conn->connection == DRM_MODE_CONNECTED &&
            conn->connector_type == DRM_MODE_CONNECTOR_USB &&
            conn->count_modes > 0) {

            drmModeModeInfo* m = &conn->modes[0];
            bool is_tb = ((m->hdisplay == 2170 || m->hdisplay == 2008) && m->vdisplay == 60) ||
                         (m->hdisplay == 60 && (m->vdisplay == 2170 || m->vdisplay == 2008));
            if (is_tb) {
                hw_width_ = m->hdisplay;
                hw_height_ = m->vdisplay;
                rotated_ = (m->hdisplay == 60);
                mode_ = *m;
                connector_id_ = conn->connector_id;

                // Find CRTC via encoder
                if (conn->encoder_id) {
                    drmModeEncoder* enc = drmModeGetEncoder(fd, conn->encoder_id);
                    if (enc) {
                        crtc_id_ = enc->crtc_id;
                        drmModeFreeEncoder(enc);
                    }
                }
                found = true;
            }
        }
        drmModeFreeConnector(conn);
    }

    drmModeFreeResources(res);

    if (!found) {
        drmDropMaster(fd);
        ::close(fd);
        return false;
    }

    fd_ = fd;
    device_path_ = path;
    return true;
}
