// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "karmanodeman.h"
#include "activekarmanode.h"
#include "addrman.h"
#include "consensus/validation.h"
#include "karmanode.h"
#include "obfuscation.h"
#include "spork.h"
#include "util.h"
#include <boost/filesystem.hpp>

#define MN_WINNER_MINIMUM_AGE 8000    // Age in seconds. This should be > KARMANODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.

/** Karmanode manager */
CKarmanodeMan mnodeman;

struct CompareLastPaid {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN {
    bool operator()(const pair<int64_t, CKarmanode>& t1,
        const pair<int64_t, CKarmanode>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CKarmanodeDB
//

CKarmanodeDB::CKarmanodeDB()
{
    pathMN = GetDataDir() / "mncache.dat";
    strMagicMessage = "KarmanodeCache";
}

bool CKarmanodeDB::Write(const CKarmanodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssKarmanodes(SER_DISK, CLIENT_VERSION);
    ssKarmanodes << strMagicMessage;                   // karmanode cache file specific magic message
    ssKarmanodes << FLATDATA(Params().MessageStart()); // network specific magic number
    ssKarmanodes << mnodemanToSave;
    uint256 hash = Hash(ssKarmanodes.begin(), ssKarmanodes.end());
    ssKarmanodes << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssKarmanodes;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    //    FileCommit(fileout);
    fileout.fclose();

    LogPrint("karmanode","Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("karmanode","  %s\n", mnodemanToSave.ToString());

    return true;
}

CKarmanodeDB::ReadResult CKarmanodeDB::Read(CKarmanodeMan& mnodemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
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

    CDataStream ssKarmanodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssKarmanodes.begin(), ssKarmanodes.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (karmanode cache file specific magic message) and ..

        ssKarmanodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid karmanode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssKarmanodes >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CKarmanodeMan object
        ssKarmanodes >> mnodemanToLoad;
    } catch (std::exception& e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("karmanode","Loaded info from mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("karmanode","  %s\n", mnodemanToLoad.ToString());
    if (!fDryRun) {
        LogPrint("karmanode","Karmanode manager - cleaning....\n");
        mnodemanToLoad.CheckAndRemove(true);
        LogPrint("karmanode","Karmanode manager - result:\n");
        LogPrint("karmanode","  %s\n", mnodemanToLoad.ToString());
    }

    return Ok;
}

void DumpKarmanodes()
{
    int64_t nStart = GetTimeMillis();

    CKarmanodeDB mndb;
    CKarmanodeMan tempMnodeman;

    LogPrint("karmanode","Verifying mncache.dat format...\n");
    CKarmanodeDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CKarmanodeDB::FileError)
        LogPrint("karmanode","Missing karmanode cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CKarmanodeDB::Ok) {
        LogPrint("karmanode","Error reading mncache.dat: ");
        if (readResult == CKarmanodeDB::IncorrectFormat)
            LogPrint("karmanode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("karmanode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("karmanode","Writting info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrint("karmanode","Karmanode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CKarmanodeMan::CKarmanodeMan()
{
    nDsqCount = 0;
}

bool CKarmanodeMan::Add(CKarmanode& mn)
{
    LOCK(cs);

    if (!mn.IsEnabled())
        return false;

    CKarmanode* pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("karmanode", "CKarmanodeMan: Adding new Karmanode %s - %i now\n", mn.vin.prevout.hash.ToString(), size() + 1);
        vKarmanodes.push_back(mn);
        return true;
    }

    return false;
}

void CKarmanodeMan::AskForMN(CNode* pnode, CTxIn& vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForKarmanodeListEntry.find(vin.prevout);
    if (i != mWeAskedForKarmanodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrint("karmanode", "CKarmanodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.prevout.hash.ToString());
    pnode->PushMessage(NetMsgType::DSEG, vin);
    int64_t askAgain = GetTime() + KARMANODE_MIN_MNP_SECONDS;
    mWeAskedForKarmanodeListEntry[vin.prevout] = askAgain;
}

void CKarmanodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        mn.Check();
    }
}

void CKarmanodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    vector<CKarmanode>::iterator it = vKarmanodes.begin();
    while (it != vKarmanodes.end()) {
        if ((*it).activeState == CKarmanode::KARMANODE_REMOVE ||
            (*it).activeState == CKarmanode::KARMANODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CKarmanode::KARMANODE_EXPIRED) ||
            (*it).protocolVersion < karmanodePayments.GetMinKarmanodePaymentsProto()) {
            LogPrint("karmanode", "CKarmanodeMan: Removing inactive Karmanode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new mnb
            map<uint256, CKarmanodeBroadcast>::iterator it3 = mapSeenKarmanodeBroadcast.begin();
            while (it3 != mapSeenKarmanodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    karmanodeSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenKarmanodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this karmanode again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForKarmanodeListEntry.begin();
            while (it2 != mWeAskedForKarmanodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForKarmanodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vKarmanodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Karmanode list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForKarmanodeList.begin();
    while (it1 != mAskedUsForKarmanodeList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForKarmanodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Karmanode list
    it1 = mWeAskedForKarmanodeList.begin();
    while (it1 != mWeAskedForKarmanodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForKarmanodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Karmanodes we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForKarmanodeListEntry.begin();
    while (it2 != mWeAskedForKarmanodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForKarmanodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenKarmanodeBroadcast
    map<uint256, CKarmanodeBroadcast>::iterator it3 = mapSeenKarmanodeBroadcast.begin();
    while (it3 != mapSeenKarmanodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (KARMANODE_REMOVAL_SECONDS * 2)) {
            mapSeenKarmanodeBroadcast.erase(it3++);
            karmanodeSync.mapSeenSyncMNB.erase((*it3).second.GetHash());
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenKarmanodePing
    map<uint256, CKarmanodePing>::iterator it4 = mapSeenKarmanodePing.begin();
    while (it4 != mapSeenKarmanodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (KARMANODE_REMOVAL_SECONDS * 2)) {
            mapSeenKarmanodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void CKarmanodeMan::Clear()
{
    LOCK(cs);
    vKarmanodes.clear();
    mAskedUsForKarmanodeList.clear();
    mWeAskedForKarmanodeList.clear();
    mWeAskedForKarmanodeListEntry.clear();
    mapSeenKarmanodeBroadcast.clear();
    mapSeenKarmanodePing.clear();
    nDsqCount = 0;
}

int CKarmanodeMan::stable_size ()
{
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nKarmanode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nKarmanode_Age = 0;

    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        if (mn.protocolVersion < nMinProtocol) {
            continue; // Skip obsolete versions
        }
        if (IsSporkActive (SPORK_8_KARMANODE_PAYMENT_ENFORCEMENT)) {
            nKarmanode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nKarmanode_Age) < nKarmanode_Min_Age) {
                continue; // Skip karmanodes younger than (default) 8000 sec (MUST be > KARMANODE_REMOVAL_SECONDS)
            }
        }
        mn.Check ();
        if (!mn.IsEnabled ())
            continue; // Skip not-enabled karmanodes

        nStable_size++;
    }

    return nStable_size;
}

int CKarmanodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? karmanodePayments.GetMinKarmanodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        mn.Check();
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CKarmanodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? karmanodePayments.GetMinKarmanodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        mn.Check();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
            case 1 :
                ipv4++;
                break;
            case 2 :
                ipv6++;
                break;
            case 3 :
                onion++;
                break;
        }
    }
}

void CKarmanodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForKarmanodeList.find(pnode->addr);
            if (it != mWeAskedForKarmanodeList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint("karmanode", "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return;
                }
            }
        }
    }

    pnode->PushMessage(NetMsgType::DSEG, CTxIn());
    int64_t askAgain = GetTime() + KARMANODES_DSEG_SECONDS;
    mWeAskedForKarmanodeList[pnode->addr] = askAgain;
}

CKarmanode* CKarmanodeMan::Find(const CScript& payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        payee2 = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (payee2 == payee)
            return &mn;
    }
    return NULL;
}

CKarmanode* CKarmanodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}


CKarmanode* CKarmanodeMan::Find(const CPubKey& pubKeyKarmanode)
{
    LOCK(cs);

    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        if (mn.pubKeyKarmanode == pubKeyKarmanode)
            return &mn;
    }
    return NULL;
}

//
// Deterministically select the oldest/best karmanode to pay on the network
//
CKarmanode* CKarmanodeMan::GetNextKarmanodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CKarmanode* pBestKarmanode = NULL;
    std::vector<pair<int64_t, CTxIn> > vecKarmanodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        mn.Check();
        if (!mn.IsEnabled()) continue;

        // //check protocol version
        if (mn.protocolVersion < karmanodePayments.GetMinKarmanodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (karmanodePayments.IsScheduled(mn, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are karmanodes
        if (mn.GetKarmanodeInputAge() < nMnCount) continue;

        vecKarmanodeLastPaid.push_back(make_pair(mn.SecondsSincePayment(), mn.vin));
    }

    nCount = (int)vecKarmanodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && nCount < nMnCount / 3) return GetNextKarmanodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecKarmanodeLastPaid.rbegin(), vecKarmanodeLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled() / 10;
    int nCountTenth = 0;
    uint256 nHigh = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecKarmanodeLastPaid) {
        CKarmanode* pmn = Find(s.second);
        if (!pmn) break;

        uint256 n = pmn->CalculateScore(1, nBlockHeight - 100);
        if (n > nHigh) {
            nHigh = n;
            pBestKarmanode = pmn;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork) break;
    }
    return pBestKarmanode;
}

CKarmanode* CKarmanodeMan::FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? karmanodePayments.GetMinKarmanodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrint("karmanode", "CKarmanodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if (nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrint("karmanode", "CKarmanodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        found = false;
        BOOST_FOREACH (CTxIn& usedVin, vecToExclude) {
            if (mn.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if (found) continue;
        if (--rand < 1) {
            return &mn;
        }
    }

    return NULL;
}

CKarmanode* CKarmanodeMan::GetCurrentMasterNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CKarmanode* winner = NULL;

    // scan for winner
    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        mn.Check();
        if (mn.protocolVersion < minProtocol || !mn.IsEnabled()) continue;

        // calculate the score for each Karmanode
        uint256 n = mn.CalculateScore(mod, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        // determine the winner
        if (n2 > score) {
            score = n2;
            winner = &mn;
        }
    }

    return winner;
}

int CKarmanodeMan::GetKarmanodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecKarmanodeScores;
    int64_t nKarmanode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nKarmanode_Age = 0;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return -1;

    // scan for winner
    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        if (mn.protocolVersion < minProtocol) {
            LogPrint("karmanode","Skipping Karmanode with obsolete version %d\n", mn.protocolVersion);
            continue;                                                       // Skip obsolete versions
        }

        if (IsSporkActive(SPORK_8_KARMANODE_PAYMENT_ENFORCEMENT)) {
            nKarmanode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nKarmanode_Age) < nKarmanode_Min_Age) {
                if (fDebug) LogPrint("karmanode","Skipping just activated Karmanode. Age: %ld\n", nKarmanode_Age);
                continue;                                                   // Skip karmanodes younger than (default) 1 hour
            }
        }
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }
        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecKarmanodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecKarmanodeScores.rbegin(), vecKarmanodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecKarmanodeScores) {
        rank++;
        if (s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

std::vector<pair<int, CKarmanode> > CKarmanodeMan::GetKarmanodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CKarmanode> > vecKarmanodeScores;
    std::vector<pair<int, CKarmanode> > vecKarmanodeRanks;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return vecKarmanodeRanks;

    // scan for winner
    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        mn.Check();

        if (mn.protocolVersion < minProtocol) continue;

        if (!mn.IsEnabled()) {
            vecKarmanodeScores.push_back(make_pair(9999, mn));
            continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecKarmanodeScores.push_back(make_pair(n2, mn));
    }

    sort(vecKarmanodeScores.rbegin(), vecKarmanodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CKarmanode) & s, vecKarmanodeScores) {
        rank++;
        vecKarmanodeRanks.push_back(make_pair(rank, s.second));
    }

    return vecKarmanodeRanks;
}

CKarmanode* CKarmanodeMan::GetKarmanodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecKarmanodeScores;

    // scan for winner
    BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
        if (mn.protocolVersion < minProtocol) continue;
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecKarmanodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecKarmanodeScores.rbegin(), vecKarmanodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecKarmanodeScores) {
        rank++;
        if (rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

void CKarmanodeMan::ProcessKarmanodeConnections()
{
    //we don't care about this for regtest
    if (Params().NetworkID() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (pnode->fObfuScationMaster) {
            if (obfuScationPool.pSubmittedToKarmanode != NULL && pnode->addr == obfuScationPool.pSubmittedToKarmanode->addr) continue;
            LogPrint("karmanode","Closing Karmanode connection peer=%i \n", pnode->GetId());
            pnode->fObfuScationMaster = false;
            pnode->Release();
        }
    }
}

void CKarmanodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return; //disable all Obfuscation/Karmanode related functionality
    if (!karmanodeSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == NetMsgType::MNB) { //Karmanode Broadcast
        CKarmanodeBroadcast mnb;
        vRecv >> mnb;

        if (mapSeenKarmanodeBroadcast.count(mnb.GetHash())) { //seen
            karmanodeSync.AddedKarmanodeList(mnb.GetHash());
            return;
        }
        mapSeenKarmanodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));

        int nDoS = 0;
        if (!mnb.CheckAndUpdate(nDoS)) {
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);

            //failed
            return;
        }

        // make sure the vout that was signed is related to the transaction that spawned the Karmanode
        //  - this is expensive, so it's only done once per Karmanode
        if (!obfuScationSigner.IsVinAssociatedWithPubkey(mnb.vin, mnb.pubKeyCollateralAddress)) {
            LogPrintf("CKarmanodeMan::ProcessMessage() : mnb - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 33);
            return;
        }

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()
        if (mnb.CheckInputsAndAdd(nDoS)) {
            // use this as a peer
            addrman.Add(CAddress(mnb.addr), pfrom->addr, 2 * 60 * 60);
            karmanodeSync.AddedKarmanodeList(mnb.GetHash());
        } else {
            LogPrint("karmanode","mnb - Rejected Karmanode entry %s\n", mnb.vin.prevout.hash.ToString());

            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == NetMsgType::MNP) { //Karmanode Ping
        CKarmanodePing mnp;
        vRecv >> mnp;

        LogPrint("karmanode", "mnp - Karmanode ping, vin: %s\n", mnp.vin.prevout.hash.ToString());

        if (mapSeenKarmanodePing.count(mnp.GetHash())) return; //seen
        mapSeenKarmanodePing.insert(make_pair(mnp.GetHash(), mnp));

        int nDoS = 0;
        if (mnp.CheckAndUpdate(nDoS)) return;

        if (nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing Karmanode list
            CKarmanode* pmn = Find(mnp.vin);
            // if it's known, don't ask for the mnb, just return
            if (pmn != NULL) return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a karmanode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == NetMsgType::DSEG) { //Get Karmanode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if (!isLocal && Params().NetworkID() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForKarmanodeList.find(pfrom->addr);
                if (i != mAskedUsForKarmanodeList.end()) {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        LogPrintf("CKarmanodeMan::ProcessMessage() : dseg - peer already asked me for the list\n");
                        Misbehaving(pfrom->GetId(), 34);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + KARMANODES_DSEG_SECONDS;
                mAskedUsForKarmanodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
            if (mn.addr.IsRFC1918()) continue; //local network

            if (mn.IsEnabled()) {
                LogPrint("karmanode", "dseg - Sending Karmanode entry - %s \n", mn.vin.prevout.hash.ToString());
                if (vin == CTxIn() || vin == mn.vin) {
                    CKarmanodeBroadcast mnb = CKarmanodeBroadcast(mn);
                    uint256 hash = mnb.GetHash();
                    pfrom->PushInventory(CInv(MSG_KARMANODE_ANNOUNCE, hash));
                    nInvCount++;

                    if (!mapSeenKarmanodeBroadcast.count(hash)) mapSeenKarmanodeBroadcast.insert(make_pair(hash, mnb));

                    if (vin == mn.vin) {
                        LogPrint("karmanode", "dseg - Sent 1 Karmanode entry to peer %i\n", pfrom->GetId());
                        return;
                    }
                }
            }
        }

        if (vin == CTxIn()) {
            pfrom->PushMessage(NetMsgType::SSC, KARMANODE_SYNC_LIST, nInvCount);
            LogPrint("karmanode", "dseg - Sent %d Karmanode entries to peer %i\n", nInvCount, pfrom->GetId());
        }
    }
    /*
     * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
     * AFTER MIGRATION TO V12 IS DONE
     */

    // Light version for OLD MASSTERNODES - fake pings, no self-activation
    else if (strCommand == NetMsgType::DSEE) { //ObfuScation Election Entry

        if (IsSporkActive(SPORK_10_KARMANODE_PAY_UPDATED_NODES)) return;

        CTxIn vin;
        CService addr;
        CPubKey pubkey;
        CPubKey pubkey2;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        int count;
        int current;
        int64_t lastUpdated;
        int protocolVersion;
        CScript donationAddress;
        int donationPercentage;
        std::string strMessage;

        vRecv >> vin >> addr >> vchSig >> sigTime >> pubkey >> pubkey2 >> count >> current >> lastUpdated >> protocolVersion >> donationAddress >> donationPercentage;

        // make sure signature isn't in the future (past is OK)
        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrintf("CKarmanodeMan::ProcessMessage() : dsee - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

        strMessage = addr.ToString() + std::to_string(sigTime) + vchPubKey + vchPubKey2 + std::to_string(protocolVersion) + donationAddress.ToString() + std::to_string(donationPercentage);

        if (protocolVersion < karmanodePayments.GetMinKarmanodePaymentsProto()) {
            LogPrintf("CKarmanodeMan::ProcessMessage() : dsee - ignoring outdated Karmanode %s protocol version %d < %d\n", vin.prevout.hash.ToString(), protocolVersion, karmanodePayments.GetMinKarmanodePaymentsProto());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        CScript pubkeyScript;
        pubkeyScript = GetScriptForDestination(pubkey.GetID());

        if (pubkeyScript.size() != 25) {
            LogPrintf("CKarmanodeMan::ProcessMessage() : dsee - pubkey the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        CScript pubkeyScript2;
        pubkeyScript2 = GetScriptForDestination(pubkey2.GetID());

        if (pubkeyScript2.size() != 25) {
            LogPrintf("CKarmanodeMan::ProcessMessage() : dsee - pubkey2 the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if (!vin.scriptSig.empty()) {
            LogPrintf("CKarmanodeMan::ProcessMessage() : dsee - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        std::string errorMessage = "";
        if (!obfuScationSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage)) {
            LogPrintf("CKarmanodeMan::ProcessMessage() : dsee - Got bad Karmanode address signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (addr.GetPort() != 52020) return;
        } else if (addr.GetPort() == 52020)
            return;

        //search existing Karmanode list, this is where we update existing Karmanodes with new dsee broadcasts
        CKarmanode* pmn = this->Find(vin);
        if (pmn != NULL) {
            // count == -1 when it's a new entry
            //   e.g. We don't want the entry relayed/time updated when we're syncing the list
            // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
            //   after that they just need to match
            if (count == -1 && pmn->pubKeyCollateralAddress == pubkey && (GetAdjustedTime() - pmn->nLastDsee > KARMANODE_MIN_MNB_SECONDS)) {
                if (pmn->protocolVersion > GETHEADERS_VERSION && sigTime - pmn->lastPing.sigTime < KARMANODE_MIN_MNB_SECONDS) return;
                if (pmn->nLastDsee < sigTime) { //take the newest entry
                    LogPrint("karmanode", "dsee - Got updated entry for %s\n", vin.prevout.hash.ToString());
                    if (pmn->protocolVersion < GETHEADERS_VERSION) {
                        pmn->pubKeyKarmanode = pubkey2;
                        pmn->sigTime = sigTime;
                        pmn->sig = vchSig;
                        pmn->protocolVersion = protocolVersion;
                        pmn->addr = addr;
                        //fake ping
                        pmn->lastPing = CKarmanodePing(vin);
                    }
                    pmn->nLastDsee = sigTime;
                    pmn->Check();
                    if (pmn->IsEnabled()) {
                        TRY_LOCK(cs_vNodes, lockNodes);
                        if (!lockNodes) return;
                        BOOST_FOREACH (CNode* pnode, vNodes)
                            if (pnode->nVersion >= karmanodePayments.GetMinKarmanodePaymentsProto())
                                pnode->PushMessage(NetMsgType::DSEE, vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, donationAddress, donationPercentage);
                    }
                }
            }

            return;
        }

        static std::map<COutPoint, CPubKey> mapSeenDsee;
        if (mapSeenDsee.count(vin.prevout) && mapSeenDsee[vin.prevout] == pubkey) {
            LogPrint("karmanode", "dsee - already seen this vin %s\n", vin.prevout.ToString());
            return;
        }
        mapSeenDsee.insert(make_pair(vin.prevout, pubkey));
        // make sure the vout that was signed is related to the transaction that spawned the Karmanode
        //  - this is expensive, so it's only done once per Karmanode
        if (!obfuScationSigner.IsVinAssociatedWithPubkey(vin, pubkey)) {
            LogPrintf("CKarmanodeMan::ProcessMessage() : dsee - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }


        LogPrint("karmanode", "dsee - Got NEW OLD Karmanode entry %s\n", vin.prevout.hash.ToString());

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()

        CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut((MASTER_NODE_AMOUNT-0.01) * COIN, obfuScationPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        bool fAcceptable = false;
        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;
            fAcceptable = AcceptableInputs(mempool, state, CTransaction(tx), false, NULL);
        }

        if (fAcceptable) {
            if (GetInputAge(vin) < KARMANODE_MIN_CONFIRMATIONS) {
                LogPrintf("CKarmanodeMan::ProcessMessage() : dsee - Input must have least %d confirmations\n", KARMANODE_MIN_CONFIRMATIONS);
                Misbehaving(pfrom->GetId(), 20);
                return;
            }

            // verify that sig time is legit in past
            // should be at least not earlier than block when 10000 OHMC tx got KARMANODE_MIN_CONFIRMATIONS
            uint256 hashBlock = 0;
            CTransaction tx2;
            GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
            BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 10000 OHMC tx -> 1 confirmation
                CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + KARMANODE_MIN_CONFIRMATIONS - 1]; // block where tx got KARMANODE_MIN_CONFIRMATIONS
                if (pConfIndex->GetBlockTime() > sigTime) {
                    LogPrint("karmanode","mnb - Bad sigTime %d for Karmanode %s (%i conf block is at %d)\n",
                        sigTime, vin.prevout.hash.ToString(), KARMANODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
                    return;
                }
            }

            // use this as a peer
            addrman.Add(CAddress(addr), pfrom->addr, 2 * 60 * 60);

            // add Karmanode
            CKarmanode mn = CKarmanode();
            mn.addr = addr;
            mn.vin = vin;
            mn.pubKeyCollateralAddress = pubkey;
            mn.sig = vchSig;
            mn.sigTime = sigTime;
            mn.pubKeyKarmanode = pubkey2;
            mn.protocolVersion = protocolVersion;
            // fake ping
            mn.lastPing = CKarmanodePing(vin);
            mn.Check(true);
            // add v11 karmanodes, v12 should be added by mnb only
            if (protocolVersion < GETHEADERS_VERSION) {
                LogPrint("karmanode", "dsee - Accepted OLD Karmanode entry %i %i\n", count, current);
                Add(mn);
            }
            if (mn.IsEnabled()) {
                TRY_LOCK(cs_vNodes, lockNodes);
                if (!lockNodes) return;
                BOOST_FOREACH (CNode* pnode, vNodes)
                    if (pnode->nVersion >= karmanodePayments.GetMinKarmanodePaymentsProto())
                        pnode->PushMessage(NetMsgType::DSEE, vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, donationAddress, donationPercentage);
            }
        } else {
            LogPrint("karmanode","dsee - Rejected Karmanode entry %s\n", vin.prevout.hash.ToString());

            int nDoS = 0;
            if (state.IsInvalid(nDoS)) {
                LogPrint("karmanode","dsee - %s from %i %s was not accepted into the memory pool\n", tx.GetHash().ToString().c_str(),
                    pfrom->GetId(), pfrom->cleanSubVer.c_str());
                if (nDoS > 0)
                    Misbehaving(pfrom->GetId(), nDoS);
            }
        }
    }

    else if (strCommand == NetMsgType::DSEEP) { //ObfuScation Election Entry Ping

        if (IsSporkActive(SPORK_10_KARMANODE_PAY_UPDATED_NODES)) return;

        CTxIn vin;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        bool stop;
        vRecv >> vin >> vchSig >> sigTime >> stop;

        //LogPrint("karmanode","dseep - Received: vin: %s sigTime: %lld stop: %s\n", vin.ToString().c_str(), sigTime, stop ? "true" : "false");

        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrintf("CKarmanodeMan::ProcessMessage() : dseep - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        if (sigTime <= GetAdjustedTime() - 60 * 60) {
            LogPrintf("CKarmanodeMan::ProcessMessage() : dseep - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        std::map<COutPoint, int64_t>::iterator i = mWeAskedForKarmanodeListEntry.find(vin.prevout);
        if (i != mWeAskedForKarmanodeListEntry.end()) {
            int64_t t = (*i).second;
            if (GetTime() < t) return; // we've asked recently
        }

        // see if we have this Karmanode
        CKarmanode* pmn = this->Find(vin);
        if (pmn != NULL && pmn->protocolVersion >= karmanodePayments.GetMinKarmanodePaymentsProto()) {
            // LogPrint("karmanode","dseep - Found corresponding mn for vin: %s\n", vin.ToString().c_str());
            // take this only if it's newer
            if (sigTime - pmn->nLastDseep > KARMANODE_MIN_MNP_SECONDS) {
                std::string strMessage = pmn->addr.ToString() + std::to_string(sigTime) + std::to_string(stop);

                std::string errorMessage = "";
                if (!obfuScationSigner.VerifyMessage(pmn->pubKeyKarmanode, vchSig, strMessage, errorMessage)) {
                    LogPrint("karmanode","dseep - Got bad Karmanode address signature %s \n", vin.prevout.hash.ToString());
                    //Misbehaving(pfrom->GetId(), 100);
                    return;
                }

                // fake ping for v11 karmanodes, ignore for v12
                if (pmn->protocolVersion < GETHEADERS_VERSION) pmn->lastPing = CKarmanodePing(vin);
                pmn->nLastDseep = sigTime;
                pmn->Check();
                if (pmn->IsEnabled()) {
                    TRY_LOCK(cs_vNodes, lockNodes);
                    if (!lockNodes) return;
                    LogPrint("karmanode", "dseep - relaying %s \n", vin.prevout.hash.ToString());
                    BOOST_FOREACH (CNode* pnode, vNodes)
                        if (pnode->nVersion >= karmanodePayments.GetMinKarmanodePaymentsProto())
                            pnode->PushMessage(NetMsgType::DSEEP, vin, vchSig, sigTime, stop);
                }
            }
            return;
        }

        LogPrint("karmanode", "dseep - Couldn't find Karmanode entry %s peer=%i\n", vin.prevout.hash.ToString(), pfrom->GetId());

        AskForMN(pfrom, vin);
    }

    /*
     * END OF "REMOVE"
     */
}

void CKarmanodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CKarmanode>::iterator it = vKarmanodes.begin();
    while (it != vKarmanodes.end()) {
        if ((*it).vin == vin) {
            LogPrint("karmanode", "CKarmanodeMan: Removing Karmanode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);
            vKarmanodes.erase(it);
            break;
        }
        ++it;
    }
}

void CKarmanodeMan::UpdateKarmanodeList(CKarmanodeBroadcast mnb)
{
    mapSeenKarmanodePing.insert(make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenKarmanodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));
    karmanodeSync.AddedKarmanodeList(mnb.GetHash());

    LogPrint("karmanode","CKarmanodeMan::UpdateKarmanodeList -- karmanode=%s\n", mnb.vin.prevout.ToStringShort());

    CKarmanode* pmn = Find(mnb.vin);
    if (pmn == NULL) {
        CKarmanode mn(mnb);
        Add(mn);
    } else {
    	pmn->UpdateFromNewBroadcast(mnb);
    }
}

std::string CKarmanodeMan::ToString() const
{
    std::ostringstream info;

    info << "Karmanodes: " << (int)vKarmanodes.size() << ", peers who asked us for Karmanode list: " << (int)mAskedUsForKarmanodeList.size() << ", peers we asked for Karmanode list: " << (int)mWeAskedForKarmanodeList.size() << ", entries in Karmanode list we asked for: " << (int)mWeAskedForKarmanodeListEntry.size() << ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}
