// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2016 The Dash developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef ACTIVEKARMANODE_H
#define ACTIVEKARMANODE_H

#include "init.h"
#include "key.h"
#include "karmanode.h"
#include "net.h"
#include "privatesend.h"
#include "sync.h"
#include "wallet.h"

#define ACTIVE_KARMANODE_INITIAL 0 // initial state
#define ACTIVE_KARMANODE_SYNC_IN_PROCESS 1
#define ACTIVE_KARMANODE_INPUT_TOO_NEW 2
#define ACTIVE_KARMANODE_NOT_CAPABLE 3
#define ACTIVE_KARMANODE_STARTED 4

// Responsible for activating the Karmanode and pinging the network
class CActiveKarmanode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    /// Ping Karmanode
    bool SendKarmanodePing(std::string& errorMessage);

    /// Register any Karmanode
    bool Register(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyKarmanode, CPubKey pubKeyKarmanode, std::string& errorMessage);

    /// Get 10000 OHMC input that can be used for the Karmanode
    bool GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

public:
    // Initialized by init.cpp
    // Keys for the main Karmanode
    CPubKey pubKeyKarmanode;

    // Initialized while registering Karmanode
    CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActiveKarmanode()
    {
        status = ACTIVE_KARMANODE_INITIAL;
    }

    /// Manage status of main Karmanode
    void ManageStatus();
    std::string GetStatus();

    /// Register remote Karmanode
    bool Register(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage);

    /// Get 10000 OHMC input that can be used for the Karmanode
    bool GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    vector<COutput> SelectCoinsKarmanode();

    /// Enable cold wallet mode (run a Karmanode with no funds)
    bool EnableHotColdMasterNode(CTxIn& vin, CService& addr);
};

#endif
