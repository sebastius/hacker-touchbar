#pragma once

// The Touch Bar's own backlight: 0 off, 1 dim, 2 full. Cycling skips
// 0 so the bar can't be switched into an invisible state and saved
// that way.
class BarBacklight {
public:
    static int nextLevel(int level) { return level >= 2 ? 1 : 2; }

    bool available() const;
    int level() const;  // -1 when unavailable
    void set(int level);
    void cycle() { set(nextLevel(level())); }
};
