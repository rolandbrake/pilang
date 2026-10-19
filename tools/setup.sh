#!/usr/bin/env sh

set -eu

install_with_apt() {
    sudo apt-get update
    sudo apt-get install -y build-essential pkg-config \
        libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-mixer-dev
}

install_with_dnf() {
    sudo dnf install -y gcc make pkgconf-pkg-config \
        SDL2-devel SDL2_image-devel SDL2_ttf-devel SDL2_mixer-devel
}

install_with_pacman() {
    sudo pacman -S --needed base-devel pkgconf sdl2 sdl2_image sdl2_ttf sdl2_mixer
}

install_with_brew() {
    brew install make pkg-config sdl2 sdl2_image sdl2_ttf sdl2_mixer
}

if command -v apt-get >/dev/null 2>&1; then
    install_with_apt
elif command -v dnf >/dev/null 2>&1; then
    install_with_dnf
elif command -v pacman >/dev/null 2>&1; then
    install_with_pacman
elif command -v brew >/dev/null 2>&1; then
    install_with_brew
else
    echo "No supported package manager was found." >&2
    echo "Install a C compiler, make, pkg-config, SDL2, SDL2_image, SDL2_ttf, and SDL2_mixer, then run make release." >&2
    exit 1
fi

echo "Pilang build dependencies are ready. Run: make release"
