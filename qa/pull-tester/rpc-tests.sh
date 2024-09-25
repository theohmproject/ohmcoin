#!/bin/bash
set -e

CURDIR=$(cd $(dirname "$0"); pwd)
# Get BUILDDIR and REAL_BITCOIND
. "${CURDIR}/tests-config.sh"

export OHMCOINCLI=${BUILDDIR}/qa/pull-tester/run-bitcoin-cli
export OHMCOIND=${REAL_BITCOIND}

if [ "x${EXEEXT}" = "x.exe" ]; then
  echo "Win tests currently disabled"
  exit 0
fi

#Run the tests

if [ "x${ENABLE_BITCOIND}${ENABLE_UTILS}${ENABLE_WALLET}" = "x111" ]; then
  ${BUILDDIR}/test/functional/wallet.py
  ${BUILDDIR}/test/functional/segwit.py
else
  echo "No rpc tests to run. Wallet, utils, and bitcoind must all be enabled"
fi
