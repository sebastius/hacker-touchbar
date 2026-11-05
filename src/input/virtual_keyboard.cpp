#include "input/virtual_keyboard.hpp"
#include "util/log.hpp"
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <linux/input.h>
#include <cstring>

VirtualKeyboard::~VirtualKeyboard() {
    if (ui_) libevdev_uinput_destroy(ui_);
    if (dev_) libevdev_free(dev_);
}

bool VirtualKeyboard::init() {
    dev_ = libevdev_new();
    if (!dev_) return false;
    libevdev_set_name(dev_, "touchbar-fx keys");
    libevdev_enable_event_type(dev_, EV_KEY);

    // Any key [[controls]] can name. The BTN_ range stays out, or the
    // device would look like a mouse or joystick.
    for (int k = KEY_ESC; k < BTN_MISC; ++k)
        libevdev_enable_event_code(dev_, EV_KEY, k, nullptr);
    for (int k = KEY_OK; k < BTN_DPAD_UP; ++k)
        libevdev_enable_event_code(dev_, EV_KEY, k, nullptr);

    int rc = libevdev_uinput_create_from_device(
        dev_, LIBEVDEV_UINPUT_OPEN_MANAGED, &ui_);
    if (rc < 0) {
        log_warn("Virtual keyboard unavailable: %s", strerror(-rc));
        ui_ = nullptr;
        return false;
    }
    return true;
}

void VirtualKeyboard::send(int code, bool down) {
    if (!ui_) return;
    libevdev_uinput_write_event(ui_, EV_KEY, code, down ? 1 : 0);
    libevdev_uinput_write_event(ui_, EV_SYN, SYN_REPORT, 0);
}
