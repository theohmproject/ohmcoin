// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2016-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "spork.h"
#include "base58.h"
#include "consensus/validation.h"
#include "key.h"
#include "main.h"
#include "karmanode-budget.h"
#include "net.h"
#include "protocol.h"
#include "sync.h"
#include "sporkdb.h"
#include "util.h"

using namespace std;
using namespace boost;

class CSporkMessage;
class CSporkManager;

CSporkManager sporkManager;

std::map<uint256, CSporkMessage> mapSporks;
std::map<int, CSporkMessage> mapSporksActive;

// Ohmcoin: on startup load spork values from previous session if they exist in the sporkDB
void LoadSporksFromDB()
{
    for (int i = SPORK_START; i <= SPORK_END; ++i) {
        // Since not all spork IDs are in use, we have to exclude undefined IDs
        std::string strSpork = sporkManager.GetSporkNameByID(i);
        if (strSpork == "Unknown") {
            LogPrintf("%s : unknown spork ID %d\n", __func__, i);
            continue;
        }
        // attempt to read spork from sporkDB
        CSporkMessage spork;
        if (!pSporkDB->ReadSpork(i, spork)) {
            LogPrintf("%s : no previous value for %s found in database\n", __func__, strSpork);
            continue;
        }

        // add spork to memory
        mapSporks[spork.GetHash()] = spork;
        mapSporksActive[spork.nSporkID] = spork;
        std::time_t result = spork.nValue;
        // If SPORK Value is greater than 1,000,000 assume it's actually a Date and then convert to a more readable format
        if (spork.nValue > 1000000) {
            LogPrintf("%s : loaded spork %s with value %d : %s", __func__,
                      sporkManager.GetSporkNameByID(spork.nSporkID), spork.nValue,
                      std::ctime(&result));
        } else {
            LogPrintf("%s : loaded spork %s with value %d\n", __func__,
                      sporkManager.GetSporkNameByID(spork.nSporkID), spork.nValue);
        }
    }
}

void ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return; //disable all obfuscation/karmanode related functionality

    if (strCommand == NetMsgType::SPORK) {
        //LogPrintf("ProcessSpork::spork\n");
        CDataStream vMsg(vRecv);
        CSporkMessage spork;
        vRecv >> spork;

        if (chainActive.Tip() == NULL) return;

        // Ignore spork messages about unknown/deleted sporks
        std::string strSpork = sporkManager.GetSporkNameByID(spork.nSporkID);
        if (strSpork == "Unknown") return;

        uint256 hash = spork.GetHash();
        if (mapSporksActive.count(spork.nSporkID)) {
            if (mapSporksActive[spork.nSporkID].nTimeSigned >= spork.nTimeSigned) {
                if (fDebug) LogPrintf("spork - seen %s block %d \n", hash.ToString(), chainActive.Tip()->nHeight);
                return;
            } else {
                if (fDebug) LogPrintf("spork - got updated spork %s block %d \n", hash.ToString(), chainActive.Tip()->nHeight);
            }
        }

        LogPrintf("spork - new %s ID %d Time %d bestHeight %d\n", hash.ToString(), spork.nSporkID, spork.nValue, chainActive.Tip()->nHeight);

        if (!sporkManager.CheckSignature(spork)) {
            LogPrintf("spork - invalid signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        mapSporks[hash] = spork;
        mapSporksActive[spork.nSporkID] = spork;
        sporkManager.Relay(spork);

        // Ohmcoin: add to spork database.
        pSporkDB->WriteSpork(spork.nSporkID, spork);
    }
    if (strCommand == NetMsgType::GETSPORKS) {
        std::map<int, CSporkMessage>::iterator it = mapSporksActive.begin();

        while (it != mapSporksActive.end()) {
            pfrom->PushMessage(NetMsgType::SPORK, it->second);
            it++;
        }
    }
}


// grab the value of the spork on the network, or the default
int64_t GetSporkValue(int nSporkID)
{
    int64_t r = -1;

    if (mapSporksActive.count(nSporkID)) {
        r = mapSporksActive[nSporkID].nValue;
    } else {
        if (nSporkID == SPORK_2_SWIFTTX) r = SPORK_2_SWIFTTX_DEFAULT;
        if (nSporkID == SPORK_3_SWIFTTX_BLOCK_FILTERING) r = SPORK_3_SWIFTTX_BLOCK_FILTERING_DEFAULT;
        if (nSporkID == SPORK_5_MAX_VALUE) r = SPORK_5_MAX_VALUE_DEFAULT;
        if (nSporkID == SPORK_7_KARMANODE_SCANNING) r = SPORK_7_KARMANODE_SCANNING_DEFAULT;
        if (nSporkID == SPORK_8_KARMANODE_PAYMENT_ENFORCEMENT) r = SPORK_8_KARMANODE_PAYMENT_ENFORCEMENT_DEFAULT;
        if (nSporkID == SPORK_9_KARMANODE_BUDGET_ENFORCEMENT) r = SPORK_9_KARMANODE_BUDGET_ENFORCEMENT_DEFAULT;
        if (nSporkID == SPORK_10_KARMANODE_PAY_UPDATED_NODES) r = SPORK_10_KARMANODE_PAY_UPDATED_NODES_DEFAULT;
        if (nSporkID == SPORK_13_ENABLE_SUPERBLOCKS) r = SPORK_13_ENABLE_SUPERBLOCKS_DEFAULT;
        if (nSporkID == SPORK_14_NEW_PROTOCOL_ENFORCEMENT) r = SPORK_14_NEW_PROTOCOL_ENFORCEMENT_DEFAULT;
        if (nSporkID == SPORK_15_NEW_PROTOCOL_ENFORCEMENT_2) r = SPORK_15_NEW_PROTOCOL_ENFORCEMENT_2_DEFAULT;
        if (nSporkID == SPORK_16_NEW_PROTOCOL_ENFORCEMENT_3) r = SPORK_16_NEW_PROTOCOL_ENFORCEMENT_3_DEFAULT;
        if (nSporkID == SPORK_17_NEW_PROTOCOL_ENFORCEMENT_4) r = SPORK_17_NEW_PROTOCOL_ENFORCEMENT_4_DEFAULT;
        if (nSporkID == SPORK_18_NEW_PROTOCOL_ENFORCEMENT_5) r = SPORK_18_NEW_PROTOCOL_ENFORCEMENT_5_DEFAULT;
        if (nSporkID == SPORK_19_MN_WINNER_MINIMUM_AGE) r = SPORK_19_MN_WINNER_MINIMUM_AGE_DEFAULT;
        if (nSporkID == SPORK_20_SEGWIT_ACTIVATION) r = SPORK_20_SEGWIT_ACTIVATION_DEFAULT;
        if (nSporkID == SPORK_21_SEGWIT_ON_COINBASE) r = SPORK_21_SEGWIT_ON_COINBASE_DEFAULT;
        if (nSporkID == SPORK_22_ZEROCOIN_MAINTENANCE_MODE) r = SPORK_22_ZEROCOIN_MAINTENANCE_MODE_DEFAULT;
        if (nSporkID == SPORK_23_NEW_BLOCKTIME_ENFORCEMENT) r = SPORK_23_NEW_BLOCKTIME_ENFORCEMENT_DEFAULT;
        if (nSporkID == SPORK_24_KERNEL_EXTRA_STAKING_CHECK) r = SPORK_24_KERNEL_EXTRA_STAKING_CHECK_DEFAULT;


        if (r == -1) LogPrintf("GetSpork::Unknown Spork %d\n", nSporkID);
    }

    return r;
}

// grab the spork value, and see if it's off
bool IsSporkActive(int nSporkID)
{
    if (Params().NetworkID() == CBaseChainParams::REGTEST) {
        if(nSporkID == SPORK_13_ENABLE_SUPERBLOCKS) {
            return true;
        }
    }
    int64_t r = GetSporkValue(nSporkID);
    if (r == -1) return false;
    return r < GetTime();
}


void ReprocessBlocks(int nBlocks)
{
    std::map<uint256, int64_t>::iterator it = mapRejectedBlocks.begin();
    while (it != mapRejectedBlocks.end()) {
        //use a window twice as large as is usual for the nBlocks we want to reset
        if ((*it).second > GetTime() - (nBlocks * 60 * 5)) {
            BlockMap::iterator mi = mapBlockIndex.find((*it).first);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                LOCK(cs_main);

                CBlockIndex* pindex = (*mi).second;
                LogPrintf("ReprocessBlocks - %s\n", (*it).first.ToString());

                CValidationState state;
                ReconsiderBlock(state, pindex);
            }
        }
        ++it;
    }

    CValidationState state;
    {
        LOCK(cs_main);
        DisconnectBlocksAndReprocess(nBlocks);
    }

    if (state.IsValid()) {
        ActivateBestChain(state);
    }
}

bool CSporkManager::CheckSignature(CSporkMessage& spork)
{
    //note: need to investigate why this is failing
    std::string strMessage = std::to_string(spork.nSporkID) + std::to_string(spork.nValue) + std::to_string(spork.nTimeSigned);
    CPubKey pubkeynew(ParseHex(Params().SporkKey()));
    std::string errorMessage = "";
    if (obfuScationSigner.VerifyMessage(pubkeynew, spork.vchSig, strMessage, errorMessage)) {
        return true;
    }

    return false;
}

bool CSporkManager::Sign(CSporkMessage& spork)
{
    std::string strMessage = std::to_string(spork.nSporkID) + std::to_string(spork.nValue) + std::to_string(spork.nTimeSigned);

    CKey key2;
    CPubKey pubkey2;
    std::string errorMessage = "";

    if (!obfuScationSigner.SetKey(strMasterPrivKey, errorMessage, key2, pubkey2)) {
        LogPrintf("CKarmanodePayments::Sign - ERROR: Invalid karmanodeprivkey: '%s'\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, spork.vchSig, key2)) {
        LogPrintf("CKarmanodePayments::Sign - Sign message failed");
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubkey2, spork.vchSig, strMessage, errorMessage)) {
        LogPrintf("CKarmanodePayments::Sign - Verify message failed");
        return false;
    }

    return true;
}

bool CSporkManager::UpdateSpork(int nSporkID, int64_t nValue)
{
    CSporkMessage msg;
    msg.nSporkID = nSporkID;
    msg.nValue = nValue;
    msg.nTimeSigned = GetTime();

    if (Sign(msg)) {
        Relay(msg);
        mapSporks[msg.GetHash()] = msg;
        mapSporksActive[nSporkID] = msg;
        return true;
    }

    return false;
}

void CSporkManager::Relay(CSporkMessage& msg)
{
    CInv inv(MSG_SPORK, msg.GetHash());
    RelayInv(inv);
}

bool CSporkManager::SetPrivKey(std::string strPrivKey)
{
    CSporkMessage msg;

    // Test signing successful, proceed
    strMasterPrivKey = strPrivKey;

    Sign(msg);

    if (CheckSignature(msg)) {
        LogPrintf("CSporkManager::SetPrivKey - Successfully initialized as spork signer\n");
        return true;
    } else {
        return false;
    }
}

int CSporkManager::GetSporkIDByName(std::string strName)
{
    if (strName == "SPORK_2_SWIFTTX") return SPORK_2_SWIFTTX;
    if (strName == "SPORK_3_SWIFTTX_BLOCK_FILTERING") return SPORK_3_SWIFTTX_BLOCK_FILTERING;
    if (strName == "SPORK_5_MAX_VALUE") return SPORK_5_MAX_VALUE;
    if (strName == "SPORK_7_KARMANODE_SCANNING") return SPORK_7_KARMANODE_SCANNING;
    if (strName == "SPORK_8_KARMANODE_PAYMENT_ENFORCEMENT") return SPORK_8_KARMANODE_PAYMENT_ENFORCEMENT;
    if (strName == "SPORK_9_KARMANODE_BUDGET_ENFORCEMENT") return SPORK_9_KARMANODE_BUDGET_ENFORCEMENT;
    if (strName == "SPORK_10_KARMANODE_PAY_UPDATED_NODES") return SPORK_10_KARMANODE_PAY_UPDATED_NODES;
    if (strName == "SPORK_13_ENABLE_SUPERBLOCKS") return SPORK_13_ENABLE_SUPERBLOCKS;
    if (strName == "SPORK_14_NEW_PROTOCOL_ENFORCEMENT") return SPORK_14_NEW_PROTOCOL_ENFORCEMENT;
    if (strName == "SPORK_15_NEW_PROTOCOL_ENFORCEMENT_2") return SPORK_15_NEW_PROTOCOL_ENFORCEMENT_2;
    if (strName == "SPORK_16_NEW_PROTOCOL_ENFORCEMENT_3") return SPORK_16_NEW_PROTOCOL_ENFORCEMENT_3;
    if (strName == "SPORK_17_NEW_PROTOCOL_ENFORCEMENT_4") return SPORK_17_NEW_PROTOCOL_ENFORCEMENT_4;
    if (strName == "SPORK_18_NEW_PROTOCOL_ENFORCEMENT_5") return SPORK_18_NEW_PROTOCOL_ENFORCEMENT_5;
    if (strName == "SPORK_19_MN_WINNER_MINIMUM_AGE") return SPORK_19_MN_WINNER_MINIMUM_AGE;
    if (strName == "SPORK_20_SEGWIT_ACTIVATION") return SPORK_20_SEGWIT_ACTIVATION;
    if (strName == "SPORK_21_SEGWIT_ON_COINBASE") return SPORK_21_SEGWIT_ON_COINBASE;
    if (strName == "SPORK_22_ZEROCOIN_MAINTENANCE_MODE") return SPORK_22_ZEROCOIN_MAINTENANCE_MODE;
    if (strName == "SPORK_23_NEW_BLOCKTIME_ENFORCEMENT") return SPORK_23_NEW_BLOCKTIME_ENFORCEMENT;
    if (strName == "SPORK_24_KERNEL_EXTRA_STAKING_CHECK") return SPORK_24_KERNEL_EXTRA_STAKING_CHECK;

    return -1;
}

std::string CSporkManager::GetSporkNameByID(int id)
{
    if (id == SPORK_2_SWIFTTX) return "SPORK_2_SWIFTTX";
    if (id == SPORK_3_SWIFTTX_BLOCK_FILTERING) return "SPORK_3_SWIFTTX_BLOCK_FILTERING";
    if (id == SPORK_5_MAX_VALUE) return "SPORK_5_MAX_VALUE";
    if (id == SPORK_7_KARMANODE_SCANNING) return "SPORK_7_KARMANODE_SCANNING";
    if (id == SPORK_8_KARMANODE_PAYMENT_ENFORCEMENT) return "SPORK_8_KARMANODE_PAYMENT_ENFORCEMENT";
    if (id == SPORK_9_KARMANODE_BUDGET_ENFORCEMENT) return "SPORK_9_KARMANODE_BUDGET_ENFORCEMENT";
    if (id == SPORK_10_KARMANODE_PAY_UPDATED_NODES) return "SPORK_10_KARMANODE_PAY_UPDATED_NODES";
    if (id == SPORK_13_ENABLE_SUPERBLOCKS) return "SPORK_13_ENABLE_SUPERBLOCKS";
    if (id == SPORK_14_NEW_PROTOCOL_ENFORCEMENT) return "SPORK_14_NEW_PROTOCOL_ENFORCEMENT";
    if (id == SPORK_15_NEW_PROTOCOL_ENFORCEMENT_2) return "SPORK_15_NEW_PROTOCOL_ENFORCEMENT_2";
    if (id == SPORK_16_NEW_PROTOCOL_ENFORCEMENT_3) return "SPORK_16_NEW_PROTOCOL_ENFORCEMENT_3";
    if (id == SPORK_17_NEW_PROTOCOL_ENFORCEMENT_4) return "SPORK_17_NEW_PROTOCOL_ENFORCEMENT_4";
    if (id == SPORK_18_NEW_PROTOCOL_ENFORCEMENT_5) return "SPORK_18_NEW_PROTOCOL_ENFORCEMENT_5";
    if (id == SPORK_19_MN_WINNER_MINIMUM_AGE) return "SPORK_19_MN_WINNER_MINIMUM_AGE";
    if (id == SPORK_20_SEGWIT_ACTIVATION) return "SPORK_20_SEGWIT_ACTIVATION";
    if (id == SPORK_21_SEGWIT_ON_COINBASE) return "SPORK_21_SEGWIT_ON_COINBASE";
    if (id == SPORK_22_ZEROCOIN_MAINTENANCE_MODE) return "SPORK_22_ZEROCOIN_MAINTENANCE_MODE";
    if (id == SPORK_23_NEW_BLOCKTIME_ENFORCEMENT) return "SPORK_23_NEW_BLOCKTIME_ENFORCEMENT";
    if (id == SPORK_24_KERNEL_EXTRA_STAKING_CHECK) return "SPORK_24_KERNEL_EXTRA_STAKING_CHECK";

    return "Unknown";
}
