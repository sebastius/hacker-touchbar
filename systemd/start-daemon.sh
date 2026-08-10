#!/bin/bash
# start-daemon.sh — start the touchbar-fx daemon as root with the
# session user's context (root is required for DRM master on the
# touch bar card; HOME/XDG_RUNTIME_DIR make it read the user config
# and serve the user IPC socket).
#
# The log redirect lives here so /tmp/touchbar-fx.log is always
# root-owned — a redirect in the user shell (Hyprland exec-once)
# fights it on every other boot and the daemon then fails to start.
# The sudoers NOPASSWD drop-in covers this exact path, which lets the
# autostart run `sudo -n` (a bare `sudo -n bash -c ...` is not
# covered and silently does nothing).
export HOME=/home/buku
export XDG_RUNTIME_DIR=/run/user/1000
exec /usr/local/bin/touchbar-fx >>/tmp/touchbar-fx.log 2>&1
