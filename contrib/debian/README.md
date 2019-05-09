
Debian
====================
This directory contains files used to package ohmcoind/ohmcoin-qt
for Debian-based Linux systems. If you compile ohmcoind/ohmcoin-qt yourself, there are some useful files here.

## ohmcoin: URI support ##


ohmcoin-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install ohmcoin-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your ohmcoinqt binary to `/usr/bin`
and the `../../share/pixmaps/ohmcoin128.png` to `/usr/share/pixmaps`

ohmcoin-qt.protocol (KDE)

