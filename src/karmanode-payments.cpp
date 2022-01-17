// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "karmanode-payments.h"
#include "addrman.h"
#include "karmanode-budget.h"
#include "karmanode-sync.h"
#include "karmanodeman.h"
#include "obfuscation.h"
#include "protocol.h"
#include "spork.h"
#include "sync.h"
#include "util.h"
#include "utilmoneystr.h"
#include <boost/filesystem.hpp>

/** Object for who's going to get paid on which blocks */
CKarmanodePayments karmanodePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapKarmanodeBlocks;
CCriticalSection cs_mapKarmanodePayeeVotes;

//
// CKarmanodePaymentDB
//

CKarmanodePaymentDB::CKarmanodePaymentDB()
{
    pathDB = GetDataDir() / "mnpayments.dat";
    strMagicMessage = "KarmanodePayments";
}

bool CKarmanodePaymentDB::Write(const CKarmanodePayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // karmanode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint("karmanode","Written info to mnpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CKarmanodePaymentDB::ReadResult CKarmanodePaymentDB::Read(CKarmanodePayments& objToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (karmanode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid karmanode payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CKarmanodePayments object
        ssObj >> objToLoad;
    } catch (std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("karmanode","Loaded info from mnpayments.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("karmanode","  %s\n", objToLoad.ToString());
    if (!fDryRun) {
        LogPrint("karmanode","Karmanode payments manager - cleaning....\n");
        objToLoad.CleanPaymentList();
        LogPrint("karmanode","Karmanode payments manager - result:\n");
        LogPrint("karmanode","  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpKarmanodePayments()
{
    int64_t nStart = GetTimeMillis();

    CKarmanodePaymentDB paymentdb;
    CKarmanodePayments tempPayments;

    LogPrint("karmanode","Verifying mnpayments.dat format...\n");
    CKarmanodePaymentDB::ReadResult readResult = paymentdb.Read(tempPayments, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CKarmanodePaymentDB::FileError)
        LogPrint("karmanode","Missing budgets file - mnpayments.dat, will try to recreate\n");
    else if (readResult != CKarmanodePaymentDB::Ok) {
        LogPrint("karmanode","Error reading mnpayments.dat: ");
        if (readResult == CKarmanodePaymentDB::IncorrectFormat)
            LogPrint("karmanode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("karmanode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("karmanode","Writting info to mnpayments.dat...\n");
    paymentdb.Write(karmanodePayments);

    LogPrint("karmanode","Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return true;

    int nHeight = 0;
    if (pindexPrev->GetBlockHash() == block.hashPrevBlock) {
        nHeight = pindexPrev->nHeight + 1;
    } else { //out of order
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
            nHeight = (*mi).second->nHeight + 1;
    }

    if (nHeight == 0) {
        LogPrint("karmanode","IsBlockValueValid() : WARNING: Couldn't find previous block\n");
    }

    //LogPrintf("XX69----------> IsBlockValueValid(): nMinted: %d, nExpectedValue: %d\n", FormatMoney(nMinted), FormatMoney(nExpectedValue));

    if (!karmanodeSync.IsSynced()) { //there is no budget data to use to check anything
        //super blocks will always be on these blocks, max 100 per budgeting
        if (nHeight % GetBudgetPaymentCycleBlocks() < 100) {
            return true;
        } else {
            if (nMinted > nExpectedValue) {
                return false;
            }
        }
    } else { // we're synced and have data so check the budget schedule

        //are these blocks even enabled
        if (!IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
            return nMinted <= nExpectedValue;
        }

        if (budget.IsBudgetPaymentBlock(nHeight)) {
            //the value of the block is evaluated in CheckBlock
            return true;
        } else {
            if (nMinted > nExpectedValue) {
                return false;
            }
        }
    }

    return true;
}

bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight)
{
    TrxValidationStatus transactionStatus = TrxValidationStatus::Invalid;

    if (!karmanodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrint("mnpayments", "Client not synced, skipping block payee checks\n");
        return true;
    }

    const CTransaction& txNew = (nBlockHeight > Params().LAST_POW_BLOCK() ? block.vtx[1] : block.vtx[0]);

    //check if it's a budget block
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
        if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
            transactionStatus = budget.IsTransactionValid(txNew, nBlockHeight);
            if (transactionStatus == TrxValidationStatus::Valid) {
                 return true;
            }

            if (transactionStatus == TrxValidationStatus::Invalid) {
                LogPrint("karmanode","Invalid budget payment detected %s\n", txNew.ToString().c_str());
                if (IsSporkActive(SPORK_9_KARMANODE_BUDGET_ENFORCEMENT))
                    return false;

                LogPrint("karmanode","Budget enforcement is disabled, accepting block\n");
            }
        }
    }

    // If we end here the transaction was either TrxValidationStatus::Invalid and Budget enforcement is disabled, or
    // a double budget payment (status = TrxValidationStatus::DoublePayment) was detected, or no/not enough karmanode
    // votes (status = TrxValidationStatus::VoteThreshold) for a finalized budget were found
    // In all cases a karmanode will get the payment for this block

    //check for karmanode payee
    if (karmanodePayments.IsTransactionValid(txNew, nBlockHeight))
        return true;
    LogPrint("karmanode","Invalid mn payment detected %s\n", txNew.ToString().c_str());

    if (IsSporkActive(SPORK_8_KARMANODE_PAYMENT_ENFORCEMENT))
        return false;
    LogPrint("karmanode","Karmanode payment enforcement is disabled, accepting block\n");

    return true;
}


void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(pindexPrev->nHeight + 1)) {
        budget.FillBlockPayee(txNew, nFees, fProofOfStake);
    } else {
        karmanodePayments.FillBlockPayee(txNew, nFees, fProofOfStake);
    }
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(nBlockHeight)) {
        return budget.GetRequiredPaymentsString(nBlockHeight);
    } else {
        return karmanodePayments.GetRequiredPaymentsString(nBlockHeight);
    }
}

void CKarmanodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    bool hasPayment = true;
    CScript payee;

    //spork
    if (!karmanodePayments.GetBlockPayee(pindexPrev->nHeight + 1, payee)) {
        //no karmanode detected
        CKarmanode* winningNode = mnodeman.GetCurrentMasterNode(1);
        if (winningNode) {
            payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
        } else {
            LogPrint("karmanode","CreateNewBlock: Failed to detect karmanode to pay\n");
            hasPayment = false;
        }
    }

    CAmount blockValue = GetBlockValue(pindexPrev->nHeight);
    CAmount karmanodePayment = GetKarmanodePayment(pindexPrev->nHeight, blockValue);

    if (!fProofOfStake) {
        txNew.vout[0].nValue = blockValue - (hasPayment ? karmanodePayment : 0);
    }

    if (hasPayment) {
        if (fProofOfStake) {
            /**For Proof Of Stake vout[0] must be null
             * Stake reward can be split into many different outputs, so we must
             * use vout.size() to align with several different cases.
             * An additional output is appended as the karmanode payment
             */
            unsigned int i = txNew.vout.size();
            txNew.vout.resize(i + 1);
            txNew.vout[i].scriptPubKey = payee;
            txNew.vout[i].nValue = karmanodePayment;

            //subtract mn payment from the stake reward
			if (i == 2) {
				// Majority of cases; do it quick and move on
				txNew.vout[i - 1].nValue -= karmanodePayment;
			} else if (i > 2) {
				// special case, stake is split between (i-1) outputs
				unsigned int outputs = i-1;
				CAmount knPaymentSplit = karmanodePayment / outputs;
				CAmount knPaymentRemainder = karmanodePayment - (knPaymentSplit * outputs);
				for (unsigned int j=1; j<=outputs; j++) {
					txNew.vout[j].nValue -= knPaymentSplit;
				}
				// in case it's not an even division, take the last bit of dust from the last one
				txNew.vout[outputs].nValue -= knPaymentRemainder;
			}
        } else {
            txNew.vout.resize(2);
            txNew.vout[1].scriptPubKey = payee;
            txNew.vout[1].nValue = karmanodePayment;
        }

        CTxDestination address1;
        ExtractDestination(payee, address1);

        LogPrint("karmanode","Karmanode payment of %s to %s\n", FormatMoney(karmanodePayment).c_str(), EncodeDestination(address1).c_str());
    }
}

int CKarmanodePayments::GetMinKarmanodePaymentsProto()
{
    return ActiveProtocol();
}

void CKarmanodePayments::ProcessMessageKarmanodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (!karmanodeSync.IsBlockchainSynced()) return;

    if (fLiteMode) return; //disable all Obfuscation/Karmanode related functionality

    if (strCommand == NetMsgType::MNGET) { //Karmanode Payments Request Sync
        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (pfrom->HasFulfilledRequest(NetMsgType::MNGET)) {
                LogPrintf("CKarmanodePayments::ProcessMessageKarmanodePayments() : mnget - peer already asked me for the list\n");
                Misbehaving(pfrom->GetId(), 20);
                return;
            }
        }

        pfrom->FulfilledRequest(NetMsgType::MNGET);
        karmanodePayments.Sync(pfrom, nCountNeeded);
        LogPrint("mnpayments", "mnget - Sent Karmanode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == NetMsgType::MNW) { //Karmanode Payments Declare Winner
        //this is required in litemodef
        CKarmanodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < ActiveProtocol()) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        if (karmanodePayments.mapKarmanodePayeeVotes.count(winner.GetHash())) {
            LogPrint("mnpayments", "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            karmanodeSync.AddedKarmanodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint("mnpayments", "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError)) {
            LogPrint("karmanode","mnw - invalid message - %s\n", strError);
            return;
        }

        if (!karmanodePayments.CanVote(winner.vinKarmanode.prevout, winner.nBlockHeight)) {
            LogPrint("karmanode","mnw - karmanode already voted - %s\n", winner.vinKarmanode.prevout.ToStringShort());
            return;
        }

        if (!winner.SignatureValid()) {
            if (karmanodeSync.IsSynced()) {
                LogPrintf("CKarmanodePayments::ProcessMessageKarmanodePayments() : mnw - invalid signature\n");
                Misbehaving(pfrom->GetId(), 20);
            }
            // it could just be a non-synced karmanode
            mnodeman.AskForMN(pfrom, winner.vinKarmanode);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);

        if (karmanodePayments.AddWinningKarmanode(winner)) {
            winner.Relay();
            karmanodeSync.AddedKarmanodeWinner(winner.GetHash());
        }
    }
}

bool CKarmanodePaymentWinner::Sign(CKey& keyKarmanode, CPubKey& pubKeyKarmanode)
{
    std::string errorMessage;
    std::string strMasterNodeSignMessage;

    std::string strMessage = vinKarmanode.prevout.ToStringShort() + std::to_string(nBlockHeight) + payee.ToString();

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig, keyKarmanode)) {
        LogPrint("karmanode","CKarmanodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyKarmanode, vchSig, strMessage, errorMessage)) {
        LogPrint("karmanode","CKarmanodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return true;
}

bool CKarmanodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if (mapKarmanodeBlocks.count(nBlockHeight)) {
        return mapKarmanodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this karmanode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CKarmanodePayments::IsScheduled(CKarmanode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapKarmanodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return false;
        nHeight = chainActive.Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapKarmanodeBlocks.count(h)) {
            if (mapKarmanodeBlocks[h].GetPayee(payee)) {
                if (mnpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CKarmanodePayments::AddWinningKarmanode(CKarmanodePaymentWinner& winnerIn)
{
    uint256 blockHash = 0;
    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100)) {
        return false;
    }

    {
        LOCK2(cs_mapKarmanodePayeeVotes, cs_mapKarmanodeBlocks);

        if (mapKarmanodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

        mapKarmanodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if (!mapKarmanodeBlocks.count(winnerIn.nBlockHeight)) {
            CKarmanodeBlockPayees blockPayees(winnerIn.nBlockHeight);
            mapKarmanodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    mapKarmanodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, 1);

    return true;
}

bool CKarmanodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;
    int nKarmanode_Drift_Count = 0;

    std::string strPayeesPossible = "";

    CAmount nReward = GetBlockValue(nBlockHeight);

    if (IsSporkActive(SPORK_8_KARMANODE_PAYMENT_ENFORCEMENT)) {
        // Get a stable number of karmanodes by ignoring newly activated (< 8000 sec old) karmanodes
        nKarmanode_Drift_Count = mnodeman.stable_size() + Params().KarmanodeCountDrift();
    }
    else {
        //account for the fact that all peers do not see the same karmanode count. A allowance of being off our karmanode count is given
        //we only need to look at an increased karmanode count because as count increases, the reward decreases. This code only checks
        //for mnPayment >= required, so it only makes sense to check the max node count allowed.
        nKarmanode_Drift_Count = mnodeman.size() + Params().KarmanodeCountDrift();
    }

    CAmount requiredKarmanodePayment = GetKarmanodePayment(nBlockHeight, nReward, nKarmanode_Drift_Count);

    //require at least 6 signatures
    for (CKarmanodePayee& payee : vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    for (CKarmanodePayee& payee : vecPayments) {
        bool found = false;
        for (CTxOut out : txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                if(out.nValue >= requiredKarmanodePayment)
                    found = true;
                else
                    LogPrint("karmanode","Karmanode payment is out of drift range. Paid=%s Min=%s\n", FormatMoney(out.nValue).c_str(), FormatMoney(requiredKarmanodePayment).c_str());
            }
        }

        if (payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);

            if (strPayeesPossible == "") {
                strPayeesPossible += EncodeDestination(address1);
            } else {
                strPayeesPossible += "," + EncodeDestination(address1);
            }
        }
    }

    LogPrint("karmanode","CKarmanodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredKarmanodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CKarmanodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    for (CKarmanodePayee& payee : vecPayments) {
        CTxDestination address;
        ExtractDestination(payee.scriptPubKey, address);

        if (ret != "Unknown") {
            ret += ", " + EncodeDestination(address) + ":" + std::to_string(payee.nVotes);
        } else {
            ret = EncodeDestination(address) + ":" + std::to_string(payee.nVotes);
        }
    }

    return ret;
}

std::string CKarmanodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapKarmanodeBlocks);

    if (mapKarmanodeBlocks.count(nBlockHeight)) {
        return mapKarmanodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CKarmanodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapKarmanodeBlocks);

    if (mapKarmanodeBlocks.count(nBlockHeight)) {
        return mapKarmanodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CKarmanodePayments::CleanPaymentList()
{
    LOCK2(cs_mapKarmanodePayeeVotes, cs_mapKarmanodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(mnodeman.size() * 1.25), 1000);

    std::map<uint256, CKarmanodePaymentWinner>::iterator it = mapKarmanodePayeeVotes.begin();
    while (it != mapKarmanodePayeeVotes.end()) {
        CKarmanodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CKarmanodePayments::CleanPaymentList - Removing old Karmanode payment - block %d\n", winner.nBlockHeight);
            karmanodeSync.mapSeenSyncMNW.erase((*it).first);
            mapKarmanodePayeeVotes.erase(it++);
            mapKarmanodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool CKarmanodePaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    CKarmanode* pmn = mnodeman.Find(vinKarmanode);

    if (!pmn) {
        strError = strprintf("Unknown Karmanode %s", vinKarmanode.prevout.hash.ToString());
        LogPrint("karmanode","CKarmanodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinKarmanode);
        return false;
    }

    if (pmn->protocolVersion < ActiveProtocol()) {
        strError = strprintf("Karmanode protocol too old %d - req %d", pmn->protocolVersion, ActiveProtocol());
        LogPrint("karmanode","CKarmanodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = mnodeman.GetKarmanodeRank(vinKarmanode, nBlockHeight - 100, ActiveProtocol());

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have karmanodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Karmanode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint("karmanode","CKarmanodePaymentWinner::IsValid - %s\n", strError);
            //if (karmanodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CKarmanodePayments::ProcessBlock(int nBlockHeight)
{
    if (!fMasterNode) return false;

    //reference node - hybrid mode

    int n = mnodeman.GetKarmanodeRank(activeKarmanode.vin, nBlockHeight - 100, ActiveProtocol());

    if (n == -1) {
        LogPrint("mnpayments", "CKarmanodePayments::ProcessBlock - Unknown Karmanode\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CKarmanodePayments::ProcessBlock - Karmanode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight) return false;

    CKarmanodePaymentWinner newWinner(activeKarmanode.vin);

    if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
        //is budget payment block -- handled by the budgeting software
    } else {
        LogPrint("karmanode","CKarmanodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeKarmanode.vin.prevout.hash.ToString());

        // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CKarmanode* pmn = mnodeman.GetNextKarmanodeInQueueForPayment(nBlockHeight, true, nCount);

        if (pmn != NULL) {
            LogPrint("karmanode","CKarmanodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
            newWinner.AddPayee(payee);

            CTxDestination address;
            ExtractDestination(payee, address);

            LogPrint("karmanode","CKarmanodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", EncodeDestination(address).c_str(), newWinner.nBlockHeight);
        } else {
            LogPrint("karmanode","CKarmanodePayments::ProcessBlock() Failed to find karmanode to pay\n");
        }
    }

    std::string errorMessage;
    CPubKey pubKeyKarmanode;
    CKey keyKarmanode;

    if (!obfuScationSigner.SetKey(strMasterNodePrivKey, errorMessage, keyKarmanode, pubKeyKarmanode)) {
        LogPrint("karmanode","CKarmanodePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrint("karmanode","CKarmanodePayments::ProcessBlock() - Signing Winner\n");
    if (newWinner.Sign(keyKarmanode, pubKeyKarmanode)) {
        LogPrint("karmanode","CKarmanodePayments::ProcessBlock() - AddWinningKarmanode\n");

        if (AddWinningKarmanode(newWinner)) {
            newWinner.Relay();
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CKarmanodePaymentWinner::Relay()
{
    CInv inv(MSG_KARMANODE_WINNER, GetHash());
    RelayInv(inv);
}

bool CKarmanodePaymentWinner::SignatureValid()
{
    CKarmanode* pmn = mnodeman.Find(vinKarmanode);

    if (pmn != NULL) {
        std::string strMessage = vinKarmanode.prevout.ToStringShort() + std::to_string(nBlockHeight) + payee.ToString();

        std::string errorMessage = "";
        if (!obfuScationSigner.VerifyMessage(pmn->pubKeyKarmanode, vchSig, strMessage, errorMessage)) {
            return error("CKarmanodePaymentWinner::SignatureValid() - Got bad Karmanode address signature %s\n", vinKarmanode.prevout.hash.ToString());
        }

        return true;
    }

    return false;
}

void CKarmanodePayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapKarmanodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    int nCount = (mnodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CKarmanodePaymentWinner>::iterator it = mapKarmanodePayeeVotes.begin();
    while (it != mapKarmanodePayeeVotes.end()) {
        CKarmanodePaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_KARMANODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    node->PushMessage(NetMsgType::SSC, KARMANODE_SYNC_MNW, nInvCount);
}

std::string CKarmanodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapKarmanodePayeeVotes.size() << ", Blocks: " << (int)mapKarmanodeBlocks.size();

    return info.str();
}


int CKarmanodePayments::GetOldestBlock()
{
    LOCK(cs_mapKarmanodeBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CKarmanodeBlockPayees>::iterator it = mapKarmanodeBlocks.begin();
    while (it != mapKarmanodeBlocks.end()) {
        if ((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}


int CKarmanodePayments::GetNewestBlock()
{
    LOCK(cs_mapKarmanodeBlocks);

    int nNewestBlock = 0;

    std::map<int, CKarmanodeBlockPayees>::iterator it = mapKarmanodeBlocks.begin();
    while (it != mapKarmanodeBlocks.end()) {
        if ((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
