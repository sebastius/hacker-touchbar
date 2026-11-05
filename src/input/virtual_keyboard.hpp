#pragma once

struct libevdev;
struct libevdev_uinput;

// uinput keyboard that turns Touch Bar buttons into real key events.
class VirtualKeyboard {
public:
    ~VirtualKeyboard();
    bool init();
    void send(int code, bool down);

private:
    struct libevdev* dev_ = nullptr;
    struct libevdev_uinput* ui_ = nullptr;
};
