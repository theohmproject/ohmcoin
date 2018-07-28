Mac OS X Build Instructions and Notes
====================================
The commands in this guide should be executed in a Terminal application.
The built-in one is located in `/Applications/Utilities/Terminal.app`.

Preparation
-----------
Install the OS X command line tools:

`xcode-select --install`

When the popup appears, click `Install`.

Then install [Homebrew](https://brew.sh).

Dependencies
----------------------

    brew install automake berkeley-db4 libtool boost@1.57 miniupnpc openssl pkg-config protobuf python3 qt libevent qrencode

See [dependencies.md](dependencies.md) for a complete overview.

If you want to build the disk image with `make deploy` (.dmg / optional), you need RSVG

    brew install librsvg

NOTE: Building with Qt4 is still supported, however, could result in a broken UI. Building with Qt5 is recommended.

Berkeley DB
-----------
It is recommended to use Berkeley DB 4.8. If you have to build it yourself,
you can use [the installation script included in contrib/](/contrib/install_db4.sh)
like so

```shell
./contrib/install_db4.sh .
```

from the root of the repository.

**Note**: You only need Berkeley DB if the wallet is enabled (see the section *Disable-Wallet mode* below).

Build Ohmcoin Core
------------------------

1. Clone the OhmCoin source code and cd into `ohmcoin`

        git clone https://github.com/theohmproject/ohmcoin
        cd ohmcoin

2.  Build ohmcoin-core:

    Configure and build the headless ohmcoin binaries as well as the GUI (if Qt is found).

    You can disable the GUI build by passing `--without-gui` to configure.

        ./autogen.sh
        ./configure
        make

3.  It is recommended to build and run the unit tests:

        make check
        
4.  Install:

        make install
        
5. (optional) You can also create a .dmg that contains the .app bundle:

        make deploy

Running
-------

Ohmcoin Core is now available at `/usr/local/bin/ohmcd`

Before running, it's recommended you create an RPC configuration file.

    echo -e "rpcuser=bitcoinrpc\nrpcpassword=$(xxd -l 16 -p /dev/urandom)" > "/Users/${USER}/Library/Application Support/OHMC/ohmc.conf"

    chmod 600 "/Users/${USER}/Library/Application Support/OHMC/ohmc.conf"

The first time you run ohmcd, it will start downloading the blockchain. This process could take several hours.

You can monitor the download process by looking at the debug.log file:

    tail -f $HOME/Library/Application\ Support/OHMC/debug.log

Other commands:
-------

    ohmcd -daemon # Starts the ohmcoin daemon.
    ohmc-cli --help # Outputs a list of command-line options.
    ohmc-cli help # Outputs a list of RPC commands when the daemon is running.

Notes
-----

* Tested on Mac OS X 10.11 through macOS 10.13 on 64-bit Intel processors only.
    - Although, it is possiable to build on older versions of  64-bit only "Mac OS X", brew does not work "easily" or at all with these
    olders versions and using MacPorts has simular issues. Instructions for building on older versions are depreciated and thus are
    unsupported. If one were to explore this issue, boost-1.57 headers must be patched, Berkeley-DB-4.8 must be patched as well and qt-5
    that is just a mess.

* Building with downloaded Qt binaries is not officially supported.
