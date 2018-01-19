
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The OHMC developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PRIVATESEND_RELAY_H
#define PRIVATESEND_RELAY_H

#include "activekarmanode.h"
#include "main.h"
#include "karmanodeman.h"


class CObfuScationRelay
{
public:
    CTxIn vinKarmanode;
    vector<unsigned char> vchSig;
    vector<unsigned char> vchSig2;
    int nBlockHeight;
    int nRelayType;
    CTxIn in;
    CTxOut out;

    CObfuScationRelay();
    CObfuScationRelay(CTxIn& vinKarmanodeIn, vector<unsigned char>& vchSigIn, int nBlockHeightIn, int nRelayTypeIn, CTxIn& in2, CTxOut& out2);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vinKarmanode);
        READWRITE(vchSig);
        READWRITE(vchSig2);
        READWRITE(nBlockHeight);
        READWRITE(nRelayType);
        READWRITE(in);
        READWRITE(out);
    }

    std::string ToString();

    bool Sign(std::string strSharedKey);
    bool VerifyMessage(std::string strSharedKey);
    void Relay();
    void RelayThroughNode(int nRank);
};


#endif
