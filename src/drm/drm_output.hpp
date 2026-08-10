#pragma once
#include "drm/drm_device.hpp"
#include "output/output.hpp"
#include <cstdint>

class Framebuffer;

class DrmOutput : public Output {
public:
    DrmOutput(DrmDevice& dev);
    ~DrmOutput();

    bool init();
    void present(const Framebuffer& fb) override;
    void shutdown();

private:
    bool createBuffer(int index);
    void destroyBuffer(int index);

    DrmDevice& dev_;

    struct Buffer {
        uint32_t handle = 0;
        uint32_t fb_id = 0;
        uint32_t pitch = 0;
        uint64_t size = 0;
        void* map = nullptr;
    };
    Buffer bufs_[2];
    int back_ = 0;
};
