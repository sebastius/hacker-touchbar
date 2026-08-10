#!/bin/bash
# touchbar-watchdog.sh — verify the Touch Bar (05ac:8302) enumerated; retry if not.
#
# The apple_bce vhci driver drops the touch bar's port announcement on ~30% of
# boots (probe-time race). A module reload re-runs the port walk; each attempt
# succeeds ~70% of the time. We retry up to MAX_ATTEMPTS times.
#
# Must run as root, early after boot (before/around session start). Reloading
# apple_bce momentarily drops the internal keyboard/trackpad/audio — hence the
# boot-time scheduling, when disruption is minimal.
#
# NEVER force-unload (rmmod -f): with pipewire holding the aaudio PCM open it
# oopses the kernel in __aaudio_send and freezes the machine (journal ends
# with FORCED_RMMOD + "pipewire exited with preempt_count 1"). We stop the
# user's audio services first so the clean unload succeeds; if the stack is
# still busy we skip the attempt — a dark bar for one boot beats a frozen
# machine.

set -u

MAX_ATTEMPTS=3
WAIT_AFTER_RELOAD=8
TB_USER=buku  # session user whose pipewire holds the aaudio PCM

log() { echo "touchbar-watchdog: $*"; }

bar_present() {
    for f in /sys/bus/usb/devices/*/idProduct; do
        [ -r "$f" ] || continue
        [ "$(cat "$f")" = "8302" ] || continue
        d="${f%/idProduct}"
        [ -r "$d/idVendor" ] && [ "$(cat "$d/idVendor")" = "05ac" ] && return 0
    done
    return 1
}

# `systemctl --user -M user@ stop` silently fails to stop anything on
# this stack — drive the user's manager through sudo + the session bus.
SYSU() {
    sudo -u "$TB_USER" XDG_RUNTIME_DIR=/run/user/1000 \
        DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus \
        systemctl --user "$@" 2>/dev/null
}

audio_stop() {
    # No-op when the user manager isn't up yet (early boot).
    # Order matters: services first, sockets last — stopping the
    # socket first cancels the service stop and pipewire keeps the
    # aaudio PCM open, and the apple_bce unload then fails "in use".
    SYSU stop wireplumber
    SYSU stop pipewire-pulse
    SYSU stop pipewire
    SYSU --no-block stop pipewire.socket pipewire-pulse.socket
}

audio_start() {
    # wireplumber pulls in pipewire + pipewire-pulse via dependencies.
    SYSU start pipewire.socket pipewire-pulse.socket
    SYSU start wireplumber
}

if bar_present; then
    log "touch bar enumerated, nothing to do"
    exit 0
fi

log "touch bar MISSING after boot — starting recovery"

for attempt in $(seq 1 "$MAX_ATTEMPTS"); do
    log "attempt $attempt/$MAX_ATTEMPTS: reloading apple_bce stack"
    systemctl stop tiny-dfr.service 2>/dev/null
    pkill -x touchbar-fx 2>/dev/null  # holds the appletbdrm DRM fd
    audio_stop
    sleep 2

    # Unload individually: when the bar never enumerated, the appletb*
    # modules aren't loaded at all, and a combined `modprobe -r a b c`
    # aborts on the first missing one — apple_bce never gets reloaded
    # and every attempt reports a bogus "busy". Only apple_bce's
    # unload is required to succeed.
    modprobe -r appletbdrm 2>/dev/null
    modprobe -r hid_appletb_kbd 2>/dev/null
    modprobe -r hid_appletb_bl 2>/dev/null
    if ! modprobe -r apple_bce 2>/dev/null; then
        sleep 2  # PCM release can lag the service stop; one retry
        if ! modprobe -r apple_bce 2>/dev/null; then
            log "attempt $attempt: apple_bce busy — refusing to force-unload"
            audio_start
            continue
        fi
    fi
    sleep 1
    modprobe apple_bce
    modprobe appletbdrm 2>/dev/null

    sleep "$WAIT_AFTER_RELOAD"
    audio_start
    if bar_present; then
        log "RECOVERED on attempt $attempt — touch bar enumerated"
        # udev rules will start tiny-dfr via the device unit; the
        # Hyprland autostart loop starts touchbar-fx once the DRM
        # card node appears.
        exit 0
    fi
    log "attempt $attempt failed"
done

log "FAILED: touch bar did not enumerate after $MAX_ATTEMPTS attempts"
exit 1
