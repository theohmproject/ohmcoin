// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KARMANODE_PAYMENTS_H
#define KARMANODE_PAYMENTS_H

#include "key.h"
#include "main.h"
#include "karmanode.h"

using namespace std;

extern CCriticalSection cs_vecPayments;
extern CCriticalSection cs_mapKarmanodeBlocks;
extern CCriticalSection cs_mapKarmanodePayeeVotes;

class CKarmanodePayments;
class CKarmanodePaymentWinner;
class CKarmanodeBlockPayees;

extern CKarmanodePayments karmanodePayments;

#define MNPAYMENTS_SIGNATURES_REQUIRED 6
#define MNPAYMENTS_SIGNATURES_TOTAL 10

void ProcessMessageKarmanodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted);
void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake);

void DumpKarmanodePayments();

/** Save Karmanode Payment Data (mnpayments.dat)
 */
class CKarmanodePaymentDB
{
private:
    boost::filesystem::path pathDB;
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

    CKarmanodePaymentDB();
    bool Write(const CKarmanodePayments& objToSave);
    ReadResult Read(CKarmanodePayments& objToLoad, bool fDryRun = false);
};

class CKarmanodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CKarmanodePayee()
    {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CKarmanodePayee(CScript payee, int nVotesIn)
    {
        scriptPubKey = payee;
        nVotes = nVotesIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(scriptPubKey);
        READWRITE(nVotes);
    }
};

// Keep track of votes for payees from karmanodes
class CKarmanodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CKarmanodePayee> vecPayments;

    CKarmanodeBlockPayees()
    {
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CKarmanodeBlockPayees(int nBlockHeightIn)
    {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(CScript payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH (CKarmanodePayee& payee, vecPayments) {
            if (payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CKarmanodePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        BOOST_FOREACH (CKarmanodePayee& p, vecPayments) {
            if (p.nVotes > nVotes) {
                payee = p.scriptPubKey;
                nVotes = p.nVotes;
            }
        }

        return (nVotes > -1);
    }

    bool HasPayeeWithVotes(CScript payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH (CKarmanodePayee& p, vecPayments) {
            if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
    }
};

// for storing the winning payments
class CKarmanodePaymentWinner
{
public:
    CTxIn vinKarmanode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CKarmanodePaymentWinner()
    {
        nBlockHeight = 0;
        vinKarmanode = CTxIn();
        payee = CScript();
    }

    CKarmanodePaymentWinner(CTxIn vinIn)
    {
        nBlockHeight = 0;
        vinKarmanode = vinIn;
        payee = CScript();
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << payee;
        ss << nBlockHeight;
        ss << vinKarmanode.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keyKarmanode, CPubKey& pubKeyKarmanode);
    bool IsValid(CNode* pnode, std::string& strError);
    bool SignatureValid();
    void Relay();

    void AddPayee(CScript payeeIn)
    {
        payee = payeeIn;
    }


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vinKarmanode);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vchSig);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinKarmanode.ToString();
        ret += ", " + std::to_string(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + std::to_string((int)vchSig.size());
        return ret;
    }
};

//
// Karmanode Payments Class
// Keeps track of who should get paid for which blocks
//

class CKarmanodePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CKarmanodePaymentWinner> mapKarmanodePayeeVotes;
    std::map<int, CKarmanodeBlockPayees> mapKarmanodeBlocks;
    std::map<uint256, int> mapKarmanodesLastVote; //prevout.hash + prevout.n, nBlockHeight

    CKarmanodePayments()
    {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapKarmanodeBlocks, cs_mapKarmanodePayeeVotes);
        mapKarmanodeBlocks.clear();
        mapKarmanodePayeeVotes.clear();
    }

    bool AddWinningKarmanode(CKarmanodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList();
    int LastPayment(CKarmanode& mn);

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CKarmanode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outKarmanode, int nBlockHeight)
    {
        LOCK(cs_mapKarmanodePayeeVotes);

        if (mapKarmanodesLastVote.count(outKarmanode.hash + outKarmanode.n)) {
            if (mapKarmanodesLastVote[outKarmanode.hash + outKarmanode.n] == nBlockHeight) {
                return false;
            }
        }

        //record this karmanode voted
        mapKarmanodesLastVote[outKarmanode.hash + outKarmanode.n] = nBlockHeight;
        return true;
    }

    int GetMinKarmanodePaymentsProto();
    void ProcessMessageKarmanodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapKarmanodePayeeVotes);
        READWRITE(mapKarmanodeBlocks);
    }
};


#endif
