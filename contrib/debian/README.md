
Debian
====================
This directory contains files used to package ohmcd/ohmc-qt
for Debian-based Linux systems. If you compile ohmcd/ohmc-qt yourself, there are some useful files here.

## ohmc: URI support ##


ohmc-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install ohmc-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your ohmcqt binary to `/usr/bin`
and the `../../share/pixmaps/ohmc128.png` to `/usr/share/pixmaps`

ohmc-qt.protocol (KDE)

