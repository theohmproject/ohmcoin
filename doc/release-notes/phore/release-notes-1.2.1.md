0.12.0 Release notes
====================


Ohmcoin Core version 1.2.1 is now available from:

  https://ohmcoin.io/

Please report bugs using the issue tracker at github:

  https://github.com/theohmproject/ohmcoin/issues


How to Upgrade
--------------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Ohmcoin-Qt (on Mac) or
ohmcoind/ohmcoin-qt (on Linux).


1.2.1 changelog
----------------

Switched to PIVX Core version v3.0.6 - https://github.com/PIVX-Project/PIVX/releases/tag/v3.0.6
- Automated database corruption repair
- More accurate error messages
- Reduction of debug log spam
- Faster transaction searching algorithm
- Fix for possible fork regarding zOHMC

Also:
- Disabled autominting in GUI (meyer9)
- Fixed compile time warnings and support C++17 (mostly) (meyer9)
- Added a Dockerfile (diógenes)
- Updated checkpoints (meyer9)
- Fixed tests (konez2k)


Versioning Note
---------------

Ohmcoin will now be switching to a more standard semantic versioning
system (https://semver.org/). This means that in the future, the version numbers
will take the form:

    MAJOR.MINOR.PATCH

Credits
--------

Thanks to who contributed to this release, at least:

meyer9
tohsnoom
diogenes
konez2k

As well as the entire PIVX team!
