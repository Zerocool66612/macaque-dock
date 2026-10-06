# 🐒 Macaque Dock

**Macaque Dock** is a macOS-inspired application dock for **KDE Plasma 6** and **Wayland**.

Macaque Dock started from the open-source Crystal Dock codebase and has since received substantial updates, new features, interface changes, and MacaqueOS-specific improvements.

## What's New in Macaque Dock

Macaque Dock is more than a branding change. Several major features and improvements have been added.

- Launchpad-style application launcher
- macOS-inspired dock appearance
- Drag-and-drop pinned application reordering
- Custom launcher icons
- Built-in icon manager
- Downloads folder stack
- KDE system folder and file icons
- Image previews in folder stacks
- Folder item-count indicator
- Improved folder-stack layout and animations
- Configurable dock colors and transparency
- KWin blur integration
- KDE Plasma 6 and Wayland improvements
- MacaqueOS integration
- Debian/Ubuntu package support
- Numerous UI and usability improvements

## Download

Download the latest Macaque Dock release:

https://github.com/Zerocool66612/macaque-dock/releases

## Install

For compatible Debian/Ubuntu-based systems:

    sudo apt install ./macaque-dock_1.0.0-1_amd64.deb

## Build From Source

Clone Macaque Dock:

    git clone https://github.com/Zerocool66612/macaque-dock.git
    cd macaque-dock

Configure and build:

    cmake -S src -B build -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build --parallel

Install:

    sudo cmake --install build

## MacaqueOS

Macaque Dock is being developed as part of the **MacaqueOS** desktop project.

It can also be installed separately on compatible KDE Plasma 6 systems.

## Credits and Upstream

Macaque Dock is derived from the open-source **Crystal Dock** project:

https://github.com/dangvd/crystal-dock

We thank the original Crystal Dock developers and contributors. Applicable original copyright and licensing notices are retained in the source code.

## License

Macaque Dock is free and open-source software distributed under the **GNU General Public License v3 or later**.

See the repository license and individual source-file notices for complete licensing information.

---

**Macaque Dock 1.0.0**
