#pragma once

// The Touch Bar's own backlight has three hardware levels: 0 off, 1 dim,
// 2 full. Level 3 is full hardware plus a software boost (no scanline
// dimming, dim content lifted) for the terminal themes. Cycling goes
// 1 -> 2 -> 3 -> 1 and skips 0, so the bar can't be switched into an
// invisible state and saved that way.
class BarBacklight {
public:
    static int nextLevel(int level) {
        return level == 1 ? 2 : level == 2 ? 3 : level == 3 ? 1 : 2;
    }

    bool available() const;
    int level() const;  // 1-3, or 0/-1 when off or unavailable
    void set(int level);
    void cycle() { set(nextLevel(level())); }
    bool boosted() const { return boost_; }

private:
    bool boost_ = false;
};
