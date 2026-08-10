#pragma once
#include "output/output.hpp"
#include <string>

// Null "dump frames" backend: every present() writes the framebuffer
// as a 24-bit BMP (frame_0001.bmp, ...) into a directory, for
// headless verification without touch bar hardware.
class DumpOutput : public Output {
public:
    explicit DumpOutput(const std::string& dir);

    void present(const Framebuffer& fb) override;
    int framesWritten() const { return count_; }

private:
    bool writeBmp(const Framebuffer& fb, const std::string& path);

    std::string dir_;
    int count_ = 0;
};
