// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "main.h"
#include "activekarmanode.h"
#include "karmanode-sync.h"
#include "karmanode-payments.h"
#include "karmanode-budget.h"
#include "karmanode.h"
#include "karmanodeman.h"
#include "spork.h"
#include "util.h"
#include "addrman.h"
// clang-format on

class CKarmanodeSync;
CKarmanodeSync karmanodeSync;

CKarmanodeSync::CKarmanodeSync()
{
    Reset();
}

bool CKarmanodeSync::IsSynced()
{
    return RequestedKarmanodeAssets == KARMANODE_SYNC_FINISHED;
}

bool CKarmanodeSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if (GetTime() - lastProcess > 60 * 60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = GetTime();

    if (fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();
    if (pindex == NULL) return false;


    if (pindex->nTime + 60 * 60 < GetTime())
        return false;

    fBlockchainSynced = true;

    return true;
}

void CKarmanodeSync::Reset()
{
    lastKarmanodeList = 0;
    lastKarmanodeWinner = 0;
    lastBudgetItem = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncMNW.clear();
    mapSeenSyncBudget.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumKarmanodeList = 0;
    sumKarmanodeWinner = 0;
    sumBudgetItemProp = 0;
    sumBudgetItemFin = 0;
    countKarmanodeList = 0;
    countKarmanodeWinner = 0;
    countBudgetItemProp = 0;
    countBudgetItemFin = 0;
    RequestedKarmanodeAssets = KARMANODE_SYNC_INITIAL;
    RequestedKarmanodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CKarmanodeSync::AddedKarmanodeList(uint256 hash)
{
    if (mnodeman.mapSeenKarmanodeBroadcast.count(hash)) {
        if (mapSeenSyncMNB[hash] < KARMANODE_SYNC_THRESHOLD) {
            lastKarmanodeList = GetTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        lastKarmanodeList = GetTime();
        mapSeenSyncMNB.insert(make_pair(hash, 1));
    }
}

void CKarmanodeSync::AddedKarmanodeWinner(uint256 hash)
{
    if (karmanodePayments.mapKarmanodePayeeVotes.count(hash)) {
        if (mapSeenSyncMNW[hash] < KARMANODE_SYNC_THRESHOLD) {
            lastKarmanodeWinner = GetTime();
            mapSeenSyncMNW[hash]++;
        }
    } else {
        lastKarmanodeWinner = GetTime();
        mapSeenSyncMNW.insert(make_pair(hash, 1));
    }
}

void CKarmanodeSync::AddedBudgetItem(uint256 hash)
{
    if (budget.mapSeenKarmanodeBudgetProposals.count(hash) || budget.mapSeenKarmanodeBudgetVotes.count(hash) ||
        budget.mapSeenFinalizedBudgets.count(hash) || budget.mapSeenFinalizedBudgetVotes.count(hash)) {
        if (mapSeenSyncBudget[hash] < KARMANODE_SYNC_THRESHOLD) {
            lastBudgetItem = GetTime();
            mapSeenSyncBudget[hash]++;
        }
    } else {
        lastBudgetItem = GetTime();
        mapSeenSyncBudget.insert(make_pair(hash, 1));
    }
}

bool CKarmanodeSync::IsBudgetPropEmpty()
{
    return sumBudgetItemProp == 0 && countBudgetItemProp > 0;
}

bool CKarmanodeSync::IsBudgetFinEmpty()
{
    return sumBudgetItemFin == 0 && countBudgetItemFin > 0;
}

void CKarmanodeSync::GetNextAsset()
{
    switch (RequestedKarmanodeAssets) {
    case (KARMANODE_SYNC_INITIAL):
    case (KARMANODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
        ClearFulfilledRequest();
        RequestedKarmanodeAssets = KARMANODE_SYNC_SPORKS;
        break;
    case (KARMANODE_SYNC_SPORKS):
        RequestedKarmanodeAssets = KARMANODE_SYNC_LIST;
        break;
    case (KARMANODE_SYNC_LIST):
        RequestedKarmanodeAssets = KARMANODE_SYNC_MNW;
        break;
    case (KARMANODE_SYNC_MNW):
        RequestedKarmanodeAssets = KARMANODE_SYNC_BUDGET;
        break;
    case (KARMANODE_SYNC_BUDGET):
        LogPrintf("CKarmanodeSync::GetNextAsset - Sync has finished\n");
        RequestedKarmanodeAssets = KARMANODE_SYNC_FINISHED;
        break;
    }
    RequestedKarmanodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CKarmanodeSync::GetSyncStatus()
{
    switch (karmanodeSync.RequestedKarmanodeAssets) {
    case KARMANODE_SYNC_INITIAL:
        return _("Synchronization pending...");
    case KARMANODE_SYNC_SPORKS:
        return _("Synchronizing sporks...");
    case KARMANODE_SYNC_LIST:
        return _("Synchronizing karmanodes...");
    case KARMANODE_SYNC_MNW:
        return _("Synchronizing karmanode winners...");
    case KARMANODE_SYNC_BUDGET:
        return _("Synchronizing budgets...");
    case KARMANODE_SYNC_FAILED:
        return _("Synchronization failed");
    case KARMANODE_SYNC_FINISHED:
        return _("Synchronization finished");
    }
    return "";
}

void CKarmanodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == NetMsgType::SSC) { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if (RequestedKarmanodeAssets >= KARMANODE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch (nItemID) {
        case (KARMANODE_SYNC_LIST):
            if (nItemID != RequestedKarmanodeAssets) return;
            sumKarmanodeList += nCount;
            countKarmanodeList++;
            break;
        case (KARMANODE_SYNC_MNW):
            if (nItemID != RequestedKarmanodeAssets) return;
            sumKarmanodeWinner += nCount;
            countKarmanodeWinner++;
            break;
        case (KARMANODE_SYNC_BUDGET_PROP):
            if (RequestedKarmanodeAssets != KARMANODE_SYNC_BUDGET) return;
            sumBudgetItemProp += nCount;
            countBudgetItemProp++;
            break;
        case (KARMANODE_SYNC_BUDGET_FIN):
            if (RequestedKarmanodeAssets != KARMANODE_SYNC_BUDGET) return;
            sumBudgetItemFin += nCount;
            countBudgetItemFin++;
            break;
        }

        LogPrint("karmanode", "CKarmanodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CKarmanodeSync::ClearFulfilledRequest()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        pnode->ClearFulfilledRequest(NetMsgType::GETSPORK);
        pnode->ClearFulfilledRequest("knsync");
        pnode->ClearFulfilledRequest("knwsync");
        pnode->ClearFulfilledRequest("busync");
    }
}

void CKarmanodeSync::Process()
{
    static int tick = 0;

    if (tick++ % KARMANODE_SYNC_TIMEOUT != 0) return;

    if (IsSynced()) {
        /* 
            Resync if we lose all karmanodes from sleep/wake or failure to sync originally
        */
        if (mnodeman.CountEnabled() == 0) {
            Reset();
        } else
            return;
    }

    //try syncing again
    if (RequestedKarmanodeAssets == KARMANODE_SYNC_FAILED && lastFailure + (1 * 60) < GetTime()) {
        Reset();
    } else if (RequestedKarmanodeAssets == KARMANODE_SYNC_FAILED) {
        return;
    }

    LogPrint("karmanode", "CKarmanodeSync::Process() - tick %d RequestedKarmanodeAssets %d\n", tick, RequestedKarmanodeAssets);

    if (RequestedKarmanodeAssets == KARMANODE_SYNC_INITIAL) GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if (Params().NetworkID() != CBaseChainParams::REGTEST &&
        !IsBlockchainSynced() && RequestedKarmanodeAssets > KARMANODE_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (Params().NetworkID() == CBaseChainParams::REGTEST) {
            if (RequestedKarmanodeAttempt <= 2) {
                pnode->PushMessage(NetMsgType::GETSPORKS); //get current network sporks
            } else if (RequestedKarmanodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if (RequestedKarmanodeAttempt < 6) {
                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage(NetMsgType::MNGET, nMnCount); //sync payees
                uint256 n = 0;
                pnode->PushMessage(NetMsgType::MNVS, n); //sync karmanode votes
            } else {
                RequestedKarmanodeAssets = KARMANODE_SYNC_FINISHED;
            }
            RequestedKarmanodeAttempt++;
            return;
        }

        // set to synced
        if (RequestedKarmanodeAssets == KARMANODE_SYNC_SPORKS) {
            if (pnode->HasFulfilledRequest("getspork")) continue;
            pnode->FulfilledRequest("getspork");

            pnode->PushMessage("getsporks"); //get current network sporks
            if (RequestedKarmanodeAttempt >= 2) GetNextAsset();
            RequestedKarmanodeAttempt++;
 
            return;
         }


        if (pnode->nVersion >= karmanodePayments.GetMinKarmanodePaymentsProto()) {
            if (RequestedKarmanodeAssets == KARMANODE_SYNC_LIST) {
                LogPrint("karmanode", "CKarmanodeSync::Process() - lastKarmanodeList %lld (GetTime() - KARMANODE_SYNC_TIMEOUT) %lld\n", lastKarmanodeList, GetTime() - KARMANODE_SYNC_TIMEOUT);
                if (lastKarmanodeList > 0 && lastKarmanodeList < GetTime() - KARMANODE_SYNC_TIMEOUT * 2 && RequestedKarmanodeAttempt >= KARMANODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("knsync")) continue;
                pnode->FulfilledRequest("knsync");

                // timeout
                if (lastKarmanodeList == 0 &&
                    (RequestedKarmanodeAttempt >= KARMANODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > KARMANODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_KARMANODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CKarmanodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedKarmanodeAssets = KARMANODE_SYNC_FAILED;
                        RequestedKarmanodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedKarmanodeAttempt >= KARMANODE_SYNC_THRESHOLD * 3) return;

                mnodeman.DsegUpdate(pnode);
                RequestedKarmanodeAttempt++;
                return;
            }

            if (RequestedKarmanodeAssets == KARMANODE_SYNC_MNW) {
                if (lastKarmanodeWinner > 0 && lastKarmanodeWinner < GetTime() - KARMANODE_SYNC_TIMEOUT * 2 && RequestedKarmanodeAttempt >= KARMANODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("knwsync")) continue;
                pnode->FulfilledRequest("knwsync");

                // timeout
                if (lastKarmanodeWinner == 0 &&
                    (RequestedKarmanodeAttempt >= KARMANODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > KARMANODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_KARMANODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CKarmanodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedKarmanodeAssets = KARMANODE_SYNC_FAILED;
                        RequestedKarmanodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedKarmanodeAttempt >= KARMANODE_SYNC_THRESHOLD * 3) return;

                CBlockIndex* pindexPrev = chainActive.Tip();
                if (pindexPrev == NULL) return;

                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage(NetMsgType::MNGET, nMnCount); //sync payees
                RequestedKarmanodeAttempt++;

                return;
            }
        }

        if (pnode->nVersion >= ActiveProtocol()) {
            if (RequestedKarmanodeAssets == KARMANODE_SYNC_BUDGET) {
                
                // We'll start rejecting votes if we accidentally get set as synced too soon
                if (lastBudgetItem > 0 && lastBudgetItem < GetTime() - KARMANODE_SYNC_TIMEOUT * 2 && RequestedKarmanodeAttempt >= KARMANODE_SYNC_THRESHOLD) {
                    
                    // Hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();

                    // Try to activate our karmanode if possible
                    activeKarmanode.ManageStatus();

                    return;
                }

                // timeout
                if (lastBudgetItem == 0 &&
                    (RequestedKarmanodeAttempt >= KARMANODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > KARMANODE_SYNC_TIMEOUT * 5)) {
                    // maybe there is no budgets at all, so just finish syncing
                    GetNextAsset();
                    activeKarmanode.ManageStatus();
                    return;
                }

                if (pnode->HasFulfilledRequest("busync")) continue;
                pnode->FulfilledRequest("busync");

                if (RequestedKarmanodeAttempt >= KARMANODE_SYNC_THRESHOLD * 3) return;

                uint256 n = 0;
                pnode->PushMessage(NetMsgType::MNVS, n); //sync karmanode votes
                RequestedKarmanodeAttempt++;

                return;
            }
        }
    }
}
