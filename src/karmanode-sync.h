// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2019 The OHMC Developers 

// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef KARMANODE_SYNC_H
#define KARMANODE_SYNC_H

#define KARMANODE_SYNC_INITIAL 0
#define KARMANODE_SYNC_SPORKS 1
#define KARMANODE_SYNC_LIST 2
#define KARMANODE_SYNC_MNW 3
#define KARMANODE_SYNC_BUDGET 4
#define KARMANODE_SYNC_BUDGET_PROP 10
#define KARMANODE_SYNC_BUDGET_FIN 11
#define KARMANODE_SYNC_FAILED 998
#define KARMANODE_SYNC_FINISHED 999

#define KARMANODE_SYNC_TIMEOUT 5
#define KARMANODE_SYNC_THRESHOLD 2

class CKarmanodeSync;
extern CKarmanodeSync karmanodeSync;

//
// CKarmanodeSync : Sync karmanode assets in stages
//

class CKarmanodeSync
{
public:
    std::map<uint256, int> mapSeenSyncMNB;
    std::map<uint256, int> mapSeenSyncMNW;
    std::map<uint256, int> mapSeenSyncBudget;

    int64_t lastKarmanodeList;
    int64_t lastKarmanodeWinner;
    int64_t lastBudgetItem;
    int64_t lastFailure;
    int nCountFailures;

    // sum of all counts
    int sumKarmanodeList;
    int sumKarmanodeWinner;
    int sumBudgetItemProp;
    int sumBudgetItemFin;
    // peers that reported counts
    int countKarmanodeList;
    int countKarmanodeWinner;
    int countBudgetItemProp;
    int countBudgetItemFin;

    // Count peers we've requested the list from
    int RequestedKarmanodeAssets;
    int RequestedKarmanodeAttempt;

    // Time when current karmanode asset sync started
    int64_t nAssetSyncStarted;

    CKarmanodeSync();

    void AddedKarmanodeList(uint256 hash);
    void AddedKarmanodeWinner(uint256 hash);
    void AddedBudgetItem(uint256 hash);
    void GetNextAsset();
    std::string GetSyncStatus();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool IsBudgetFinEmpty();
    bool IsBudgetPropEmpty();

    void Reset();
    void Process();
    bool IsSynced();
    bool IsBlockchainSynced();
    bool IsKarmanodeListSynced() { return RequestedKarmanodeAssets > KARMANODE_SYNC_LIST; }
    void ClearFulfilledRequest();
};

#endif
