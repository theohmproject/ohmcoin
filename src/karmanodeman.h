// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The OHMC 
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KARMANODEMAN_H
#define KARMANODEMAN_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "karmanode.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#define KARMANODES_DUMP_SECONDS (15 * 60)
#define KARMANODES_DSEG_SECONDS (3 * 60 * 60)

using namespace std;

class CKarmanodeMan;

extern CKarmanodeMan mnodeman;
void DumpKarmanodes();

/** Access to the MN database (mncache.dat)
 */
class CKarmanodeDB
{
private:
    boost::filesystem::path pathMN;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CKarmanodeDB();
    bool Write(const CKarmanodeMan& mnodemanToSave);
    ReadResult Read(CKarmanodeMan& mnodemanToLoad, bool fDryRun = false);
};

class CKarmanodeMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs
    std::vector<CKarmanode> vKarmanodes;
    // who's asked for the Karmanode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForKarmanodeList;
    // who we asked for the Karmanode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForKarmanodeList;
    // which Karmanodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForKarmanodeListEntry;

public:
    // Keep track of all broadcasts I've seen
    map<uint256, CKarmanodeBroadcast> mapSeenKarmanodeBroadcast;
    // Keep track of all pings I've seen
    map<uint256, CKarmanodePing> mapSeenKarmanodePing;

    // keep track of dsq count to prevent karmanodes from gaming privatesend queue
    int64_t nDsqCount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);
        READWRITE(vKarmanodes);
        READWRITE(mAskedUsForKarmanodeList);
        READWRITE(mWeAskedForKarmanodeList);
        READWRITE(mWeAskedForKarmanodeListEntry);
        READWRITE(nDsqCount);

        READWRITE(mapSeenKarmanodeBroadcast);
        READWRITE(mapSeenKarmanodePing);
    }

    CKarmanodeMan();
    CKarmanodeMan(CKarmanodeMan& other);

    /// Add an entry
    bool Add(CKarmanode& mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode* pnode, CTxIn& vin);

    /// Check all Karmanodes
    void Check();

    /// Check all Karmanodes and remove inactive
    void CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear Karmanode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CKarmanode* Find(const CScript& payee);
    CKarmanode* Find(const CTxIn& vin);
    CKarmanode* Find(const CPubKey& pubKeyKarmanode);

    /// Find an entry in the karmanode list that is next to be paid
    CKarmanode* GetNextKarmanodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CKarmanode* FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion = -1);

    /// Get the current winner for this block
    CKarmanode* GetCurrentMasterNode(int mod = 1, int64_t nBlockHeight = 0, int minProtocol = 0);

    std::vector<CKarmanode> GetFullKarmanodeVector()
    {
        Check();
        return vKarmanodes;
    }

    std::vector<pair<int, CKarmanode> > GetKarmanodeRanks(int64_t nBlockHeight, int minProtocol = 0);
    int GetKarmanodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);
    CKarmanode* GetKarmanodeByRank(int nRank, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);

    void ProcessKarmanodeConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    /// Return the number of (unique) Karmanodes
    int size() { return vKarmanodes.size(); }

    /// Return the number of Karmanodes older than (default) 8000 seconds
    int stable_size ();

    std::string ToString() const;

    void Remove(CTxIn vin);

    /// Update karmanode list and maps using provided CKarmanodeBroadcast
    void UpdateKarmanodeList(CKarmanodeBroadcast mnb);
};

#endif
