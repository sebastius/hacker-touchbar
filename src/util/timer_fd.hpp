#pragma once
#include <cstdint>

// Creates a timerfd that fires after interval_ms, repeating if repeating=true.
// Returns the fd, or -1 on error.
int timerfd_create_ms(int interval_ms, bool repeating = true);

// Read and consume the timerfd expiration count. Returns number of expirations.
uint64_t timerfd_read(int fd);

// Arm/rearm a timerfd with a new interval. 0 disarms it.
void timerfd_set_ms(int fd, int interval_ms, bool repeating = true);
