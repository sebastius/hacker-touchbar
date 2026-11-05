#!/bin/sh
# Builds and installs Hacker Touchbar for the current user, or removes it.
#   ./install.sh             build, install, enable the service
#   ./install.sh uninstall   remove it; tiny-dfr gets the bar back
# Run as your normal user: it calls sudo itself.
set -eu

cd "$(dirname "$0")"
PREFIX=/usr/local
UNIT=/etc/systemd/system/touchbar-fx.service
SLEEP=/usr/lib/systemd/system-sleep/touchbar-fx

if [ "$(id -u)" = 0 ]; then
    echo "Run this as your normal user, not with sudo." >&2
    exit 1
fi

if [ "${1:-}" = uninstall ]; then
    sudo systemctl disable --now touchbar-fx.service || true
    sudo rm -f "$UNIT" "$SLEEP" "$PREFIX/bin/touchbar-fx" "$PREFIX/bin/touchbar-fxctl"
    sudo systemctl daemon-reload
    sudo systemctl start tiny-dfr.service || true
    echo "Removed. Your files in ~/.config/touchbar-fx were left alone."
    exit 0
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

sudo install -m 755 build/touchbar-fx build/touchbar-fxctl "$PREFIX/bin/"
sed -e "s/@UID@/$(id -u)/g" -e "s/@GID@/$(id -g)/g" systemd/touchbar-fx.service |
    sudo tee "$UNIT" >/dev/null
sudo chmod 644 "$UNIT"
sudo install -m 755 systemd/touchbar-fx-sleep "$SLEEP"

config="$HOME/.config/touchbar-fx/config.toml"
if [ ! -e "$config" ]; then
    mkdir -p "$(dirname "$config")"
    cp config.example.toml "$config"
    echo "Created $config"
fi

sudo systemctl daemon-reload
sudo systemctl enable touchbar-fx.service
sudo systemctl restart touchbar-fx.service
sleep 2
systemctl --no-pager --lines=0 status touchbar-fx.service
