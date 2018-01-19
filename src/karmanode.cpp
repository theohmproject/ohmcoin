// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The OHMC developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "karmanode.h"
#include "addrman.h"
#include "karmanodeman.h"
#include "privatesend.h"
#include "sync.h"
#include "util.h"
#include <boost/lexical_cast.hpp>

// keep track of the scanning errors I've seen
map<uint256, int> mapSeenKarmanodeScanningErrors;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    if (chainActive.Tip() == NULL) return false;

    if (nBlockHeight == 0)
        nBlockHeight = chainActive.Tip()->nHeight;

    if (mapCacheBlockHashes.count(nBlockHeight)) {
        hash = mapCacheBlockHashes[nBlockHeight];
        return true;
    }

    const CBlockIndex* BlockLastSolved = chainActive.Tip();
    const CBlockIndex* BlockReading = chainActive.Tip();

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || chainActive.Tip()->nHeight + 1 < nBlockHeight) return false;

    int nBlocksAgo = 0;
    if (nBlockHeight > 0) nBlocksAgo = (chainActive.Tip()->nHeight + 1) - nBlockHeight;
    assert(nBlocksAgo >= 0);

    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nBlocksAgo) {
            hash = BlockReading->GetBlockHash();
            mapCacheBlockHashes[nBlockHeight] = hash;
            return true;
        }
        n++;

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return false;
}

CKarmanode::CKarmanode()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyKarmanode = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = KARMANODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CKarmanodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    nActiveState = KARMANODE_ENABLED,
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nLastDsee = 0;  // temporary, do not save. Remove after migration to v12
    nLastDseep = 0; // temporary, do not save. Remove after migration to v12
}

CKarmanode::CKarmanode(const CKarmanode& other)
{
    LOCK(cs);
    vin = other.vin;
    addr = other.addr;
    pubKeyCollateralAddress = other.pubKeyCollateralAddress;
    pubKeyKarmanode = other.pubKeyKarmanode;
    sig = other.sig;
    activeState = other.activeState;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    cacheInputAge = other.cacheInputAge;
    cacheInputAgeBlock = other.cacheInputAgeBlock;
    unitTest = other.unitTest;
    allowFreeTx = other.allowFreeTx;
    nActiveState = KARMANODE_ENABLED,
    protocolVersion = other.protocolVersion;
    nLastDsq = other.nLastDsq;
    nScanningErrorCount = other.nScanningErrorCount;
    nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
    lastTimeChecked = 0;
    nLastDsee = other.nLastDsee;   // temporary, do not save. Remove after migration to v12
    nLastDseep = other.nLastDseep; // temporary, do not save. Remove after migration to v12
}

CKarmanode::CKarmanode(const CKarmanodeBroadcast& mnb)
{
    LOCK(cs);
    vin = mnb.vin;
    addr = mnb.addr;
    pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
    pubKeyKarmanode = mnb.pubKeyKarmanode;
    sig = mnb.sig;
    activeState = KARMANODE_ENABLED;
    sigTime = mnb.sigTime;
    lastPing = mnb.lastPing;
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    nActiveState = KARMANODE_ENABLED,
    protocolVersion = mnb.protocolVersion;
    nLastDsq = mnb.nLastDsq;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nLastDsee = 0;  // temporary, do not save. Remove after migration to v12
    nLastDseep = 0; // temporary, do not save. Remove after migration to v12
}

//
// When a new karmanode broadcast is sent, update our information
//
bool CKarmanode::UpdateFromNewBroadcast(CKarmanodeBroadcast& mnb)
{
    if (mnb.sigTime > sigTime) {
        pubKeyKarmanode = mnb.pubKeyKarmanode;
        pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
        sigTime = mnb.sigTime;
        sig = mnb.sig;
        protocolVersion = mnb.protocolVersion;
        addr = mnb.addr;
        lastTimeChecked = 0;
        int nDoS = 0;
        if (mnb.lastPing == CKarmanodePing() || (mnb.lastPing != CKarmanodePing() && mnb.lastPing.CheckAndUpdate(nDoS, false))) {
            lastPing = mnb.lastPing;
            mnodeman.mapSeenKarmanodePing.insert(make_pair(lastPing.GetHash(), lastPing));
        }
        return true;
    }
    return false;
}

//
// Deterministically calculate a given "score" for a Karmanode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
uint256 CKarmanode::CalculateScore(int mod, int64_t nBlockHeight)
{
    if (chainActive.Tip() == NULL) return 0;

    uint256 hash = 0;
    uint256 aux = vin.prevout.hash + vin.prevout.n;

    if (!GetBlockHash(hash, nBlockHeight)) {
        LogPrintf("CalculateScore ERROR - nHeight %d - Returned 0\n", nBlockHeight);
        return 0;
    }

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hash;
    uint256 hash2 = ss.GetHash();

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << hash;
    ss2 << aux;
    uint256 hash3 = ss2.GetHash();

    uint256 r = (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);

    return r;
}

void CKarmanode::Check(bool forceCheck)
{
    if (ShutdownRequested()) return;

    if (!forceCheck && (GetTime() - lastTimeChecked < KARMANODE_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();


    //once spent, stop doing the checks
    if (activeState == KARMANODE_VIN_SPENT) return;


    if (!IsPingedWithin(KARMANODE_REMOVAL_SECONDS)) {
        activeState = KARMANODE_REMOVE;
        return;
    }

    if (!IsPingedWithin(KARMANODE_EXPIRATION_SECONDS)) {
        activeState = KARMANODE_EXPIRED;
        return;
    }

    if (!unitTest) {
        CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut(9999.99 * COIN, obfuScationPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;

            if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
                activeState = KARMANODE_VIN_SPENT;
                return;
            }
        }
    }

    activeState = KARMANODE_ENABLED; // OK
}

int64_t CKarmanode::SecondsSincePayment()
{
    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    int64_t sec = (GetAdjustedTime() - GetLastPaid());
    int64_t month = 60 * 60 * 24 * 30;
    if (sec < month) return sec; //if it's less than 30 days, give seconds

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // return some deterministic value for unknown/unpaid but force it to be more than 30 days old
    return month + hash.GetCompact(false);
}

int64_t CKarmanode::GetLastPaid()
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // use a deterministic offset to break a tie -- 2.5 minutes
    int64_t nOffset = hash.GetCompact(false) % 150;

    if (chainActive.Tip() == NULL) return false;

    const CBlockIndex* BlockReading = chainActive.Tip();

    int nMnCount = mnodeman.CountEnabled() * 1.25;
    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nMnCount) {
            return 0;
        }
        n++;

        if (karmanodePayments.mapKarmanodeBlocks.count(BlockReading->nHeight)) {
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network 
                to converge on the same payees quickly, then keep the same schedule.
            */
            if (karmanodePayments.mapKarmanodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)) {
                return BlockReading->nTime + nOffset;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return 0;
}

std::string CKarmanode::GetStatus()
{
    switch (nActiveState) {
    case CKarmanode::KARMANODE_PRE_ENABLED:
        return "PRE_ENABLED";
    case CKarmanode::KARMANODE_ENABLED:
        return "ENABLED";
    case CKarmanode::KARMANODE_EXPIRED:
        return "EXPIRED";
    case CKarmanode::KARMANODE_OUTPOINT_SPENT:
        return "OUTPOINT_SPENT";
    case CKarmanode::KARMANODE_REMOVE:
        return "REMOVE";
    case CKarmanode::KARMANODE_WATCHDOG_EXPIRED:
        return "WATCHDOG_EXPIRED";
    case CKarmanode::KARMANODE_POSE_BAN:
        return "POSE_BAN";
    default:
        return "UNKNOWN";
    }
}

bool CKarmanode::IsValidNetAddr()
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkID() == CBaseChainParams::REGTEST ||
           (IsReachable(addr) && addr.IsRoutable());
}

CKarmanodeBroadcast::CKarmanodeBroadcast()
{
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyKarmanode1 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = KARMANODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CKarmanodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CKarmanodeBroadcast::CKarmanodeBroadcast(CService newAddr, CTxIn newVin, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyKarmanodeNew, int protocolVersionIn)
{
    vin = newVin;
    addr = newAddr;
    pubKeyCollateralAddress = pubKeyCollateralAddressNew;
    pubKeyKarmanode = pubKeyKarmanodeNew;
    sig = std::vector<unsigned char>();
    activeState = KARMANODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CKarmanodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = protocolVersionIn;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CKarmanodeBroadcast::CKarmanodeBroadcast(const CKarmanode& mn)
{
    vin = mn.vin;
    addr = mn.addr;
    pubKeyCollateralAddress = mn.pubKeyCollateralAddress;
    pubKeyKarmanode = mn.pubKeyKarmanode;
    sig = mn.sig;
    activeState = mn.activeState;
    sigTime = mn.sigTime;
    lastPing = mn.lastPing;
    cacheInputAge = mn.cacheInputAge;
    cacheInputAgeBlock = mn.cacheInputAgeBlock;
    unitTest = mn.unitTest;
    allowFreeTx = mn.allowFreeTx;
    protocolVersion = mn.protocolVersion;
    nLastDsq = mn.nLastDsq;
    nScanningErrorCount = mn.nScanningErrorCount;
    nLastScanningErrorBlockHeight = mn.nLastScanningErrorBlockHeight;
}

bool CKarmanodeBroadcast::Create(std::string strService, std::string strKeyKarmanode, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CKarmanodeBroadcast& mnbRet, bool fOffline)
{
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyKarmanodeNew;
    CKey keyKarmanodeNew;

    //need correct blocks to send ping
    if (!fOffline && !karmanodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Karmanode";
        LogPrintf("CKarmanodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!obfuScationSigner.GetKeysFromSecret(strKeyKarmanode, keyKarmanodeNew, pubKeyKarmanodeNew)) {
        strErrorRet = strprintf("Invalid karmanode key %s", strKeyKarmanode);
        LogPrintf("CKarmanodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!pwalletMain->GetKarmanodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for karmanode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CKarmanodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    CService service = CService(strService);
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            strErrorRet = strprintf("Invalid port %u for karmanode %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
            LogPrintf("CKarmanodeBroadcast::Create -- %s\n", strErrorRet);
            return false;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for karmanode %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
        LogPrintf("CKarmanodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    return Create(txin, CService(strService), keyCollateralAddressNew, pubKeyCollateralAddressNew, keyKarmanodeNew, pubKeyKarmanodeNew, strErrorRet, mnbRet);
}

bool CKarmanodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyKarmanodeNew, CPubKey pubKeyKarmanodeNew, std::string& strErrorRet, CKarmanodeBroadcast& mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("karmanode", "CKarmanodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyKarmanodeNew.GetID() = %s\n",
        CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
        pubKeyKarmanodeNew.GetID().ToString());


    CKarmanodePing mnp(txin);
    if (!mnp.Sign(keyKarmanodeNew, pubKeyKarmanodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, karmanode=%s", txin.prevout.hash.ToString());
        LogPrintf("CKarmanodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CKarmanodeBroadcast();
        return false;
    }

    mnbRet = CKarmanodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyKarmanodeNew, PROTOCOL_VERSION);

    ////////if (!mnbRet.IsValidNetAddr()) {
      //  strErrorRet = strprintf("Invalid IP address, karmanode=%s", txin.prevout.hash.ToString());
     //   LogPrintf("CKarmanodeBroadcast::Create -- %s\n", strErrorRet);
     //   mnbRet = CKarmanodeBroadcast();
     //   return false;
  ///  }

    mnbRet.lastPing = mnp;
    if (!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, karmanode=%s", txin.prevout.hash.ToString());
        LogPrintf("CKarmanodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CKarmanodeBroadcast();
        return false;
    }

    return true;
}

bool CKarmanodeBroadcast::CheckAndUpdate(int& nDos)
{
    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("mnb - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyKarmanode.begin(), pubKeyKarmanode.end());
    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if (protocolVersion < karmanodePayments.GetMinKarmanodePaymentsProto()) {
        LogPrintf("mnb - ignoring outdated Karmanode %s protocol version %d\n", vin.prevout.hash.ToString(), protocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25) {
        LogPrintf("mnb - pubkey the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyKarmanode.GetID());

    if (pubkeyScript2.size() != 25) {
        LogPrintf("mnb - pubkey2 the wrong size\n");
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty()) {
        LogPrintf("mnb - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
        return false;
    }

    std::string errorMessage = "";
    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, strMessage, errorMessage)) {
        LogPrintf("mnb - Got bad Karmanode address signature\n");
        nDos = 100;
        return false;
    }

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (addr.GetPort() != 52020) return false;
    } else if (addr.GetPort() == 52020)
        return false;

    //search existing Karmanode list, this is where we update existing Karmanodes with new mnb broadcasts
    CKarmanode* pmn = mnodeman.Find(vin);

    // no such karmanode, nothing to update
    if (pmn == NULL)
        return true;
    else {
        // this broadcast older than we have, it's bad.
        if (pmn->sigTime > sigTime) {
            LogPrintf("mnb - Bad sigTime %d for Karmanode %s (existing broadcast is at %d)\n",
                sigTime, vin.prevout.hash.ToString(), pmn->sigTime);
            return false;
        }
        // karmanode is not enabled yet/already, nothing to update
        if (!pmn->IsEnabled()) return true;
    }

    // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if (pmn->pubKeyCollateralAddress == pubKeyCollateralAddress && !pmn->IsBroadcastedWithin(KARMANODE_MIN_MNB_SECONDS)) {
        //take the newest entry
        LogPrint("karmanode", "mnb - Got updated entry for %s\n", vin.prevout.hash.ToString());
        if (pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            if (pmn->IsEnabled()) Relay();
        }
        karmanodeSync.AddedKarmanodeList(GetHash());
    }

    return true;
}

bool CKarmanodeBroadcast::CheckInputsAndAdd(int& nDoS)
{
    // we are a karmanode with the same vin (i.e. already activated) and this mnb is ours (matches our Karmanode privkey)
    // so nothing to do here for us
    if (fMasterNode && vin.prevout == activeKarmanode.vin.prevout && pubKeyKarmanode == activeKarmanode.pubKeyKarmanode)
        return true;

    // search existing Karmanode list
    CKarmanode* pmn = mnodeman.Find(vin);

    if (pmn != NULL) {
        // nothing to do here if we already know about this karmanode and it's enabled
        if (pmn->IsEnabled()) return true;
        // if it's not enabled, remove old MN first and continue
        else
            mnodeman.Remove(pmn->vin);
    }

    CValidationState state;
    CMutableTransaction tx = CMutableTransaction();
    CTxOut vout = CTxOut(9999.99 * COIN, obfuScationPool.collateralPubKey);
    tx.vin.push_back(vin);
    tx.vout.push_back(vout);

    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            // not mnb fault, let it to be checked again later
            mnodeman.mapSeenKarmanodeBroadcast.erase(GetHash());
            karmanodeSync.mapSeenSyncMNB.erase(GetHash());
            return false;
        }

        if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
            //set nDos
            state.IsInvalid(nDoS);
            return false;
        }
    }

    LogPrint("karmanode", "mnb - Accepted Karmanode entry\n");

    if (GetInputAge(vin) < KARMANODE_MIN_CONFIRMATIONS) {
        LogPrintf("mnb - Input must have at least %d confirmations\n", KARMANODE_MIN_CONFIRMATIONS);
        // maybe we miss few blocks, let this mnb to be checked again later
        mnodeman.mapSeenKarmanodeBroadcast.erase(GetHash());
        karmanodeSync.mapSeenSyncMNB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 OHMC tx got KARMANODE_MIN_CONFIRMATIONS
    uint256 hashBlock = 0;
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second) {
        CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 1000 OHMC tx -> 1 confirmation
        CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + KARMANODE_MIN_CONFIRMATIONS - 1]; // block where tx got KARMANODE_MIN_CONFIRMATIONS
        if (pConfIndex->GetBlockTime() > sigTime) {
            LogPrintf("mnb - Bad sigTime %d for Karmanode %s (%i conf block is at %d)\n",
                sigTime, vin.prevout.hash.ToString(), KARMANODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
            return false;
        }
    }

    LogPrintf("mnb - Got NEW Karmanode entry - %s - %lli \n", vin.prevout.hash.ToString(), sigTime);
    CKarmanode mn(*this);
    mnodeman.Add(mn);

    // if it matches our Karmanode privkey, then we've been remotely activated
    if (pubKeyKarmanode == activeKarmanode.pubKeyKarmanode && protocolVersion == PROTOCOL_VERSION) {
        activeKarmanode.EnableHotColdMasterNode(vin, addr);
    }

    bool isLocal = addr.IsRFC1918() || addr.IsLocal();
    if (Params().NetworkID() == CBaseChainParams::REGTEST) isLocal = false;

    if (!isLocal) Relay();

    return true;
}

void CKarmanodeBroadcast::Relay()
{
    CInv inv(MSG_KARMANODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

bool CKarmanodeBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string errorMessage;

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyKarmanode.begin(), pubKeyKarmanode.end());

    sigTime = GetAdjustedTime();

    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, sig, keyCollateralAddress)) {
        LogPrintf("CKarmanodeBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, strMessage, errorMessage)) {
        LogPrintf("CKarmanodeBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

CKarmanodePing::CKarmanodePing()
{
    vin = CTxIn();
    blockHash = uint256(0);
    sigTime = 0;
    vchSig = std::vector<unsigned char>();
}

CKarmanodePing::CKarmanodePing(CTxIn& newVin)
{
    vin = newVin;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}


bool CKarmanodePing::Sign(CKey& keyKarmanode, CPubKey& pubKeyKarmanode)
{
    std::string errorMessage;
    std::string strMasterNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig, keyKarmanode)) {
        LogPrintf("CKarmanodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyKarmanode, vchSig, strMessage, errorMessage)) {
        LogPrintf("CKarmanodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CKarmanodePing::CheckAndUpdate(int& nDos, bool fRequireEnabled)
{
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CKarmanodePing::CheckAndUpdate - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    if (sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrintf("CKarmanodePing::CheckAndUpdate - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
        nDos = 1;
        return false;
    }

    LogPrint("karmanode", "CKarmanodePing::CheckAndUpdate - New Ping - %s - %lli\n", blockHash.ToString(), sigTime);

    // see if we have this Karmanode
    CKarmanode* pmn = mnodeman.Find(vin);
    if (pmn != NULL && pmn->protocolVersion >= karmanodePayments.GetMinKarmanodePaymentsProto()) {
        if (fRequireEnabled && !pmn->IsEnabled()) return false;

        // LogPrintf("mnping - Found corresponding mn for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this karmanode or
        // last ping was more then KARMANODE_MIN_MNP_SECONDS-60 ago comparing to this one
        if (!pmn->IsPingedWithin(KARMANODE_MIN_MNP_SECONDS - 60, sigTime)) {
            std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

            std::string errorMessage = "";
            if (!obfuScationSigner.VerifyMessage(pmn->pubKeyKarmanode, vchSig, strMessage, errorMessage)) {
                LogPrintf("CKarmanodePing::CheckAndUpdate - Got bad Karmanode address signature %s\n", vin.prevout.hash.ToString());
                nDos = 33;
                return false;
            }

            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                if ((*mi).second->nHeight < chainActive.Height() - 24) {
                    LogPrintf("CKarmanodePing::CheckAndUpdate - Karmanode %s block hash %s is too old\n", vin.prevout.hash.ToString(), blockHash.ToString());
                    // Do nothing here (no Karmanode update, no mnping relay)
                    // Let this node to be visible but fail to accept mnping

                    return false;
                }
            } else {
                if (fDebug) LogPrintf("CKarmanodePing::CheckAndUpdate - Karmanode %s block hash %s is unknown\n", vin.prevout.hash.ToString(), blockHash.ToString());
                // maybe we stuck so we shouldn't ban this node, just fail to accept it
                // TODO: or should we also request this block?

                return false;
            }

            pmn->lastPing = *this;

            //mnodeman.mapSeenKarmanodeBroadcast.lastPing is probably outdated, so we'll update it
            CKarmanodeBroadcast mnb(*pmn);
            uint256 hash = mnb.GetHash();
            if (mnodeman.mapSeenKarmanodeBroadcast.count(hash)) {
                mnodeman.mapSeenKarmanodeBroadcast[hash].lastPing = *this;
            }

            pmn->Check(true);
            if (!pmn->IsEnabled()) return false;

            LogPrint("karmanode", "CKarmanodePing::CheckAndUpdate - Karmanode ping accepted, vin: %s\n", vin.prevout.hash.ToString());

            Relay();
            return true;
        }
        LogPrint("karmanode", "CKarmanodePing::CheckAndUpdate - Karmanode ping arrived too early, vin: %s\n", vin.prevout.hash.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint("karmanode", "CKarmanodePing::CheckAndUpdate - Couldn't find compatible Karmanode entry, vin: %s\n", vin.prevout.hash.ToString());

    return false;
}

void CKarmanodePing::Relay()
{
    CInv inv(MSG_KARMANODE_PING, GetHash());
    RelayInv(inv);
}
