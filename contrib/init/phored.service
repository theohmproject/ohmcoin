[Unit]
Description=Ohmcoin's distributed currency daemon
After=network.target

[Service]
User=ohmcoin
Group=ohmcoin

Type=forking
PIDFile=/var/lib/ohmcoind/ohmcoind.pid

ExecStart=/usr/bin/ohmcoind -daemon -pid=/var/lib/ohmcoind/ohmcoind.pid \
          -conf=/etc/ohmcoin/ohmcoin.conf -datadir=/var/lib/ohmcoind

ExecStop=-/usr/bin/ohmcoin-cli -conf=/etc/ohmcoin/ohmcoin.conf \
         -datadir=/var/lib/ohmcoind stop

Restart=always
PrivateTmp=true
TimeoutStopSec=60s
TimeoutStartSec=2s
StartLimitInterval=120s
StartLimitBurst=5

[Install]
WantedBy=multi-user.target
