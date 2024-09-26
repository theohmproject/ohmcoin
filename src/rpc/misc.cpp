// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "clientversion.h"
#include "crypto/ripemd160.h"
#include "init.h"
#include "main.h"
#include "karmanode-sync.h"
#include "net.h"
#include "netbase.h"
#include "rpc/server.h"
#include "spork.h"
#include "timedata.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif

#include <stdint.h>

#include <boost/assign/list_of.hpp>

#include <univalue.h>

using namespace boost;
using namespace boost::assign;
using namespace std;

/**
 * @note Do not add or change anything in the information returned by this
 * method. `getinfo` exists for backwards-compatibility only. It combines
 * information from wildly different sources in the program, which is a mess,
 * and is thus planned to be deprecated eventually.
 *
 * Based on the source of the information, new information should be added to:
 * - `getblockchaininfo`,
 * - `getnetworkinfo` or
 * - `getwalletinfo`
 *
 * Or alternatively, create a specific query method for the information.
 **/
UniValue getinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getinfo\n"
            "Returns an object containing various state info.\n"
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,           (numeric) the server version\n"
            "  \"protocolversion\": xxxxx,   (numeric) the protocol version\n"
            "  \"walletversion\": xxxxx,     (numeric) the wallet version\n"
            "  \"balance\": xxxxxxx,         (numeric) the total ohmcoin balance of the wallet (excluding zerocoins)\n"
            "  \"zerocoinbalance\": xxxxxxx, (numeric) the total zerocoin balance of the wallet\n"
            "  \"blocks\": xxxxxx,           (numeric) the current number of blocks processed in the server\n"
            "  \"timeoffset\": xxxxx,        (numeric) the time offset\n"
            "  \"connections\": xxxxx,       (numeric) the number of connections\n"
            "  \"proxy\": \"host:port\",     (string, optional) the proxy used by the server\n"
            "  \"difficulty\": xxxxxx,       (numeric) the current difficulty\n"
            "  \"testnet\": true|false,      (boolean) if the server is using testnet or not\n"
            "  \"moneysupply\" : \"supply\"       (numeric) The money supply when this block was added to the blockchain\n"
            "  \"zOHMCsupply\" :\n"
            "  {\n"
            "     \"1\" : n,            (numeric) supply of 1 zOHMC denomination\n"
            "     \"5\" : n,            (numeric) supply of 5 zOHMC denomination\n"
            "     \"10\" : n,           (numeric) supply of 10 zOHMC denomination\n"
            "     \"50\" : n,           (numeric) supply of 50 zOHMC denomination\n"
            "     \"100\" : n,          (numeric) supply of 100 zOHMC denomination\n"
            "     \"500\" : n,          (numeric) supply of 500 zOHMC denomination\n"
            "     \"1000\" : n,         (numeric) supply of 1000 zOHMC denomination\n"
            "     \"5000\" : n,         (numeric) supply of 5000 zOHMC denomination\n"
            "     \"total\" : n,        (numeric) The total supply of all zOHMC denominations\n"
            "  }\n"
            "  \"keypoololdest\": xxxxxx,    (numeric) the timestamp (seconds since GMT epoch) of the oldest pre-generated key in the key pool\n"
            "  \"keypoolsize\": xxxx,        (numeric) how many new keys are pre-generated\n"
            "  \"unlocked_until\": ttt,      (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "  \"paytxfee\": x.xxxx,         (numeric) the transaction fee set in ohmcoin/kb\n"
            "  \"relayfee\": x.xxxx,         (numeric) minimum relay fee for non-free transactions in ohmcoin/kb\n"
            "  \"staking status\": true|false,  (boolean) if the wallet is staking or not\n"
            "  \"errors\": \"...\"           (string) any error messages\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getinfo", "") + HelpExampleRpc("getinfo", ""));

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("version", CLIENT_VERSION));
    obj.push_back(Pair("protocolversion", PROTOCOL_VERSION));
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        obj.push_back(Pair("walletversion", pwalletMain->GetVersion()));
        obj.push_back(Pair("balance", ValueFromAmount(pwalletMain->GetBalance())));
        obj.push_back(Pair("zerocoinbalance", ValueFromAmount(pwalletMain->GetZerocoinBalance(true))));
    }
#endif
    obj.push_back(Pair("blocks", (int)chainActive.Height()));
    obj.push_back(Pair("timeoffset", GetTimeOffset()));
    obj.push_back(Pair("connections", (int)vNodes.size()));
    obj.push_back(Pair("proxy", (proxy.IsValid() ? proxy.proxy.ToStringIPPort() : string())));
    obj.push_back(Pair("difficulty", (double)GetDifficulty()));
    obj.push_back(Pair("testnet", Params().TestnetToBeDeprecatedFieldRPC()));

    // During inital block verification chainActive.Tip() might be not yet initialized
    if (chainActive.Tip() == NULL) {
        obj.push_back(Pair("status", "Blockchain information not yet available"));
        return obj;
    }

    obj.push_back(Pair("moneysupply",ValueFromAmount(chainActive.Tip()->nMoneySupply)));
    UniValue zohmcObj(UniValue::VOBJ);
    for (auto denom : libzerocoin::zerocoinDenomList) {
        zohmcObj.push_back(Pair(to_string(denom), ValueFromAmount(chainActive.Tip()->mapZerocoinSupply.at(denom) * (denom*COIN))));
    }
    zohmcObj.push_back(Pair("total", ValueFromAmount(chainActive.Tip()->GetZerocoinSupply())));
    obj.push_back(Pair("zOHMCsupply", zohmcObj));

#ifdef ENABLE_WALLET
    if (pwalletMain) {
        obj.push_back(Pair("keypoololdest", pwalletMain->GetOldestKeyPoolTime()));
        obj.push_back(Pair("keypoolsize", (int)pwalletMain->GetKeyPoolSize()));
    }
    if (pwalletMain && pwalletMain->IsCrypted())
        obj.push_back(Pair("unlocked_until", nWalletUnlockTime));
    obj.push_back(Pair("paytxfee", ValueFromAmount(payTxFee.GetFeePerK())));
#endif
    obj.push_back(Pair("relayfee", ValueFromAmount(::minRelayTxFee.GetFeePerK())));
    bool nStaking = false;
    if (mapHashedBlocks.count(chainActive.Tip()->nHeight))
        nStaking = true;
    else if (mapHashedBlocks.count(chainActive.Tip()->nHeight - 1) && nLastCoinStakeSearchInterval)
        nStaking = true;
    obj.push_back(Pair("staking status", (nStaking ? "Staking Active" : "Staking Not Active")));
    obj.push_back(Pair("errors", GetWarnings("statusbar")));
    return obj;
}

UniValue knsync(const UniValue& params, bool fHelp)
{
    std::string strMode;
    if (params.size() == 1)
        strMode = params[0].get_str();

    if (fHelp || params.size() != 1 || (strMode != "status" && strMode != "reset")) {
        throw runtime_error(
            "knsync \"status|reset\"\n"
            "\nReturns the sync status or resets sync.\n"

            "\nArguments:\n"
            "1. \"mode\"    (string, required) either 'status' or 'reset'\n"

            "\nResult ('status' mode):\n"
            "{\n"
            "  \"IsBlockchainSynced\": true|false,    (boolean) 'true' if blockchain is synced\n"
            "  \"lastKarmanodeList\": xxxx,        (numeric) Timestamp of last MN list message\n"
            "  \"lastKarmanodeWinner\": xxxx,      (numeric) Timestamp of last MN winner message\n"
            "  \"lastBudgetItem\": xxxx,            (numeric) Timestamp of last MN budget message\n"
            "  \"lastFailure\": xxxx,           (numeric) Timestamp of last failed sync\n"
            "  \"nCountFailures\": n,           (numeric) Number of failed syncs (total)\n"
            "  \"sumKarmanodeList\": n,        (numeric) Number of MN list messages (total)\n"
            "  \"sumKarmanodeWinner\": n,      (numeric) Number of MN winner messages (total)\n"
            "  \"sumBudgetItemProp\": n,        (numeric) Number of MN budget messages (total)\n"
            "  \"sumBudgetItemFin\": n,         (numeric) Number of MN budget finalization messages (total)\n"
            "  \"countKarmanodeList\": n,      (numeric) Number of MN list messages (local)\n"
            "  \"countKarmanodeWinner\": n,    (numeric) Number of MN winner messages (local)\n"
            "  \"countBudgetItemProp\": n,      (numeric) Number of MN budget messages (local)\n"
            "  \"countBudgetItemFin\": n,       (numeric) Number of MN budget finalization messages (local)\n"
            "  \"RequestedKarmanodeAssets\": n, (numeric) Status code of last sync phase\n"
            "  \"RequestedKarmanodeAttempt\": n, (numeric) Status code of last sync attempt\n"
            "}\n"

            "\nResult ('reset' mode):\n"
            "\"status\"     (string) 'success'\n"
            "\nExamples:\n" +
            HelpExampleCli("knsync", "\"status\"") + HelpExampleRpc("knsync", "\"status\""));
    }

    if (strMode == "status") {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("IsBlockchainSynced", karmanodeSync.IsBlockchainSynced()));
        obj.push_back(Pair("lastKarmanodeList", karmanodeSync.lastKarmanodeList));
        obj.push_back(Pair("lastKarmanodeWinner", karmanodeSync.lastKarmanodeWinner));
        obj.push_back(Pair("lastBudgetItem", karmanodeSync.lastBudgetItem));
        obj.push_back(Pair("lastFailure", karmanodeSync.lastFailure));
        obj.push_back(Pair("nCountFailures", karmanodeSync.nCountFailures));
        obj.push_back(Pair("sumKarmanodeList", karmanodeSync.sumKarmanodeList));
        obj.push_back(Pair("sumKarmanodeWinner", karmanodeSync.sumKarmanodeWinner));
        obj.push_back(Pair("sumBudgetItemProp", karmanodeSync.sumBudgetItemProp));
        obj.push_back(Pair("sumBudgetItemFin", karmanodeSync.sumBudgetItemFin));
        obj.push_back(Pair("countKarmanodeList", karmanodeSync.countKarmanodeList));
        obj.push_back(Pair("countKarmanodeWinner", karmanodeSync.countKarmanodeWinner));
        obj.push_back(Pair("countBudgetItemProp", karmanodeSync.countBudgetItemProp));
        obj.push_back(Pair("countBudgetItemFin", karmanodeSync.countBudgetItemFin));
        obj.push_back(Pair("RequestedKarmanodeAssets", karmanodeSync.RequestedKarmanodeAssets));
        obj.push_back(Pair("RequestedKarmanodeAttempt", karmanodeSync.RequestedKarmanodeAttempt));

        return obj;
    }

    if (strMode == "reset") {
        karmanodeSync.Reset();
        return "success";
    }
    return "failure";
}


#ifdef ENABLE_WALLET
class DescribeAddressVisitor : public boost::static_visitor<UniValue>
{
public:
    CWallet * const pwallet;

    explicit DescribeAddressVisitor(CWallet *_pwallet) : pwallet(_pwallet) {}

    UniValue operator()(const CNoDestination &dest) const { return UniValue(UniValue::VOBJ); }

    UniValue operator()(const CKeyID &keyID) const {
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        obj.push_back(Pair("isscript", false));
        obj.push_back(Pair("iswitness", false));
        if (pwallet && pwallet->GetPubKey(keyID, vchPubKey)) {
            obj.push_back(Pair("pubkey", HexStr(vchPubKey)));
            obj.push_back(Pair("iscompressed", vchPubKey.IsCompressed()));
        }
        return obj;
    }

    UniValue operator()(const CScriptID &scriptID) const {
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        obj.push_back(Pair("isscript", true));
        obj.push_back(Pair("iswitness", false));
        if (pwallet && pwallet->GetCScript(scriptID, subscript)) {
            std::vector<CTxDestination> addresses;
            txnouttype whichType;
            int nRequired;
            ExtractDestinations(subscript, whichType, addresses, nRequired);
            obj.push_back(Pair("script", GetTxnOutputType(whichType)));
            obj.push_back(Pair("hex", HexStr(subscript.begin(), subscript.end())));
            UniValue a(UniValue::VARR);
            for (const CTxDestination& addr : addresses) {
                a.push_back(EncodeDestination(addr));
            }
            obj.push_back(Pair("addresses", a));
            if (whichType == TX_MULTISIG)
                obj.push_back(Pair("sigsrequired", nRequired));
        }
        return obj;
    }

    UniValue operator()(const WitnessV0KeyHash& id) const
    {
        UniValue obj(UniValue::VOBJ);
        CPubKey pubkey;
        obj.push_back(Pair("isscript", false));
        obj.push_back(Pair("iswitness", true));
        obj.push_back(Pair("witness_version", 0));
        obj.push_back(Pair("witness_program", HexStr(id.begin(), id.end())));
        if (pwallet && pwallet->GetPubKey(CKeyID(id), pubkey)) {
            obj.push_back(Pair("pubkey", HexStr(pubkey)));
        }
        return obj;
    }

    UniValue operator()(const WitnessV0ScriptHash& id) const
    {
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        obj.push_back(Pair("isscript", true));
        obj.push_back(Pair("iswitness", true));
        obj.push_back(Pair("witness_version", 0));
        obj.push_back(Pair("witness_program", HexStr(id.begin(), id.end())));
        CRIPEMD160 hasher;
        uint160 hash;
        hasher.Write(id.begin(), 32).Finalize(hash.begin());
        if (pwallet && pwallet->GetCScript(CScriptID(hash), subscript)) {
            obj.push_back(Pair("hex", HexStr(subscript.begin(), subscript.end())));
        }
        return obj;
    }

    UniValue operator()(const WitnessUnknown& id) const
    {
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        obj.push_back(Pair("iswitness", true));
        obj.push_back(Pair("witness_version", (int)id.version));
        obj.push_back(Pair("witness_program", HexStr(id.program, id.program + id.length)));
        return obj;
    }
};
#endif

/*
    Used for updating/reading spork settings on the network
*/
UniValue spork(const UniValue& params, bool fHelp)
{
    if (params.size() == 1 && params[0].get_str() == "show") {
        UniValue ret(UniValue::VOBJ);
        for (int nSporkID = SPORK_START; nSporkID <= SPORK_END; nSporkID++) {
            if (sporkManager.GetSporkNameByID(nSporkID) != "Unknown")
                ret.push_back(Pair(sporkManager.GetSporkNameByID(nSporkID), GetSporkValue(nSporkID)));
        }
        return ret;
    } else if (params.size() == 1 && params[0].get_str() == "active") {
        UniValue ret(UniValue::VOBJ);
        for (int nSporkID = SPORK_START; nSporkID <= SPORK_END; nSporkID++) {
            if (sporkManager.GetSporkNameByID(nSporkID) != "Unknown")
                ret.push_back(Pair(sporkManager.GetSporkNameByID(nSporkID), IsSporkActive(nSporkID)));
        }
        return ret;
    } else if (params.size() == 2) {
        int nSporkID = sporkManager.GetSporkIDByName(params[0].get_str());
        if (nSporkID == -1) {
            return "Invalid spork name";
        }

        // SPORK VALUE
        int64_t nValue = params[1].get_int64();

        //broadcast new spork
        if (sporkManager.UpdateSpork(nSporkID, nValue)) {
            return "success";
        } else {
            return "failure";
        }
    }

    throw runtime_error(
        "spork <name> [<value>]\n"
        "<name> is the corresponding spork name, or 'show' to show all current spork settings, active to show which sporks are active"
        "<value> is a epoch datetime to enable or disable spork" +
        HelpRequiringPassphrase());
}

UniValue validateaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "validateaddress \"bitcoinaddress\"\n"
            "\nReturn information about the given bitcoin address.\n"
            "\nArguments:\n"
            "1. \"bitcoinaddress\"     (string, required) The bitcoin address to validate\n"
            "\nResult:\n"
            "{\n"
            "  \"isvalid\" : true|false,       (boolean) If the address is valid or not. If not, this is the only property returned.\n"
            "  \"address\" : \"bitcoinaddress\", (string) The bitcoin address validated\n"
            "  \"scriptPubKey\" : \"hex\",       (string) The hex encoded scriptPubKey generated by the address\n"
            "  \"ismine\" : true|false,        (boolean) If the address is yours or not\n"
            "  \"iswatchonly\" : true|false,   (boolean) If the address is watchonly\n"
            "  \"isscript\" : true|false,      (boolean) If the key is a script\n"
            "  \"pubkey\" : \"publickeyhex\",    (string) The hex value of the raw public key\n"
            "  \"iscompressed\" : true|false,  (boolean) If the address is compressed\n"
            "  \"account\" : \"account\"         (string) DEPRECATED. The account associated with the address, \"\" is the default account\n"
            "  \"hdkeypath\" : \"keypath\"       (string, optional) The HD keypath if the key is HD and available\n"
            "  \"hdmasterkeyid\" : \"<hash160>\" (string, optional) The Hash160 of the HD master pubkey\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("validateaddress", "\"6LR355je3nMyqa9sFqWvpJfePASDzF6dL4\"")
            + HelpExampleRpc("validateaddress", "\"6LR355je3nMyqa9sFqWvpJfePASDzF6dL4\"")
        );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    bool isValid = IsValidDestinationString(params[0].get_str());

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("isvalid", isValid));
    if (isValid)
    {
        CTxDestination dest = DecodeDestination(params[0].get_str());
        string currentAddress = EncodeDestination(dest);
        ret.push_back(Pair("address", currentAddress));

        CScript scriptPubKey = GetScriptForDestination(dest);
        ret.push_back(Pair("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end())));

#ifdef ENABLE_WALLET
        isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : ISMINE_NO;
        ret.push_back(Pair("ismine", (mine & ISMINE_SPENDABLE) ? true : false));
        ret.push_back(Pair("iswatchonly", (mine & ISMINE_WATCH_ONLY) ? true: false));
        UniValue detail = boost::apply_visitor(DescribeAddressVisitor(pwalletMain), dest);
        ret.pushKVs(detail);
        if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
            ret.push_back(Pair("account", pwalletMain->mapAddressBook[dest].name));
                CKeyID keyID;
        CHDChain hdChainCurrent;
        if (pwalletMain && pwalletMain->mapHdPubKeys.count(keyID))
        {
            ret.push_back(Pair("hdkeypath", pwalletMain->mapHdPubKeys[keyID].GetKeyPath()));
            ret.push_back(Pair("hdmasterkeyid", hdChainCurrent.GetID().GetHex()));
        }
#endif
    }
    return ret;
}

/**
 * Used by addmultisigaddress / createmultisig:
 */
CScript _createmultisig_redeemScript(const UniValue& params)
{
    int nRequired = params[0].get_int();
    const UniValue& keys = params[1].get_array();

    // Gather public keys
    if (nRequired < 1)
        throw runtime_error("a multisignature address must require at least one key to redeem");
    if ((int)keys.size() < nRequired)
        throw runtime_error(
            strprintf("not enough keys supplied "
                      "(got %u keys, but need at least %d to redeem)", keys.size(), nRequired));
    if (keys.size() > 16)
        throw runtime_error("Number of addresses involved in the multisignature address creation > 16\nReduce the number");
    std::vector<CPubKey> pubkeys;
    pubkeys.resize(keys.size());
    for (unsigned int i = 0; i < keys.size(); i++)
    {
        const std::string& ks = keys[i].get_str();
#ifdef ENABLE_WALLET
        // Case 1: Bitcoin address and we have full public key:
        LogPrintf("walletmain: %d, validdest: %d, dest: %s\n", !!pwalletMain ? 1 : 0, IsValidDestinationString(ks) ? 1 : 0, ks);
        if (pwalletMain && IsValidDestinationString(ks))
        {
            CTxDestination dest = DecodeDestination(ks);
            const CKeyStore& keystore = *pwalletMain;
            CKeyID keyID = GetKeyForDestination(keystore, dest);
            CPubKey vchPubKey;
            if (!pwalletMain->GetPubKey(keyID, vchPubKey))
                throw runtime_error(
                    strprintf("no full public key for address %s",ks));
            if (!vchPubKey.IsFullyValid())
                throw runtime_error(" Invalid public key: "+ks);
            pubkeys[i] = vchPubKey;
        }

        // Case 2: hex public key
        else
#endif
        if (IsHex(ks))
        {
            CPubKey vchPubKey(ParseHex(ks));
            if (!vchPubKey.IsFullyValid())
                throw runtime_error("Invalid public key: "+ks);
            pubkeys[i] = vchPubKey;
        }
        else
        {
            throw runtime_error("Invalid public key: "+ks);
        }
    }
    CScript result = GetScriptForMultisig(nRequired, pubkeys);

    if (result.size() > MAX_SCRIPT_ELEMENT_SIZE)
        throw runtime_error(
                strprintf("redeemScript exceeds size limit: %d > %d", result.size(), MAX_SCRIPT_ELEMENT_SIZE));

    return result;
}

UniValue createmultisig(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 2) {
        string msg = "createmultisig nrequired [\"key\",...]\n"
                     "\nCreates a multi-signature address with n signature of m keys required.\n"
                     "It returns a json object with the address and redeemScript.\n"

                     "\nArguments:\n"
                     "1. nrequired      (numeric, required) The number of required signatures out of the n keys or addresses.\n"
                     "2. \"keys\"       (string, required) A json array of keys which are ohmcoin addresses or hex-encoded public keys\n"
                     "     [\n"
                     "       \"key\"    (string) ohmcoin address or hex-encoded public key\n"
                     "       ,...\n"
                     "     ]\n"

                     "\nResult:\n"
                     "{\n"
                     "  \"address\":\"multisigaddress\",  (string) The value of the new multisig address.\n"
                     "  \"redeemScript\":\"script\"       (string) The string value of the hex-encoded redemption script.\n"
                     "}\n"

                     "\nExamples:\n"
                     "\nCreate a multisig address from 2 addresses\n" +
                     HelpExampleCli("createmultisig", "2 \"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"") +
                     "\nAs a json rpc call\n" + HelpExampleRpc("createmultisig", "2, \"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"");
        throw runtime_error(msg);
    }

    // Construct using pay-to-script-hash:
    CScript inner = _createmultisig_redeemScript(params);
    CScriptID innerID(inner);

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("address", EncodeDestination(CTxDestination(inner))));
    result.push_back(Pair("redeemScript", HexStr(inner.begin(), inner.end())));

    return result;
}

UniValue createwitnessaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1)
    {
        string msg = "createwitnessaddress \"script\"\n"
            "\nCreates a witness address for a particular script.\n"
            "It returns a json object with the address and witness script.\n"

            "\nArguments:\n"
            "1. \"script\"       (string, required) A hex encoded script\n"


            "\nResult:\n"
            "{\n"
            "  \"address\":\"multisigaddress\",  (string) The value of the new address (P2SH of witness script).\n"
            "  \"witnessScript\":\"script\"      (string) The string value of the hex-encoded witness script.\n"
            "}\n"
        ;
        throw runtime_error(msg);
    }

    if (!IsHex(params[0].get_str()))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Script must be hex-encoded");

    std::vector<unsigned char> code = ParseHex(params[0].get_str());
    CScript script(code.begin(), code.end());
    CScript witscript = GetScriptForWitness(script);
    CScriptID witscriptid(witscript);

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("address", EncodeDestination(CTxDestination(witscriptid))));
    result.push_back(Pair("witnessScript", HexStr(witscript.begin(), witscript.end())));

    return result;
}

UniValue verifymessage(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "verifymessage \"ohmcoinaddress\" \"signature\" \"message\"\n"
            "\nVerify a signed message\n"
            "\nArguments:\n"
            "1. \"ohmcoinaddress\"  (string, required) The ohmcoin address to use for the signature.\n"
            "2. \"signature\"       (string, required) The signature provided by the signer in base 64 encoding (see signmessage).\n"
            "3. \"message\"         (string, required) The message that was signed.\n"
            "\nResult:\n"
            "true|false   (boolean) If the signature is verified or not.\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n" +
            HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n" + HelpExampleCli("signmessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"my message\"") +
            "\nVerify the signature\n" + HelpExampleCli("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"signature\" \"my message\"") +
            "\nAs json rpc\n" + HelpExampleRpc("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\", \"signature\", \"my message\""));

    LOCK(cs_main);

    string strAddress = params[0].get_str();
    string strSign = params[1].get_str();
    string strMessage = params[2].get_str();


    if (!IsValidDestinationString(strAddress))

        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");

    CTxDestination addr = DecodeDestination(strAddress);

    CKeyID *keyID = boost::get<CKeyID>(&addr);
    if (!keyID)
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");

    bool fInvalid = false;
    vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == *keyID);
}

UniValue setmocktime(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "setmocktime timestamp\n"
            "\nSet the local time to given timestamp (-regtest only)\n"
            "\nArguments:\n"
            "1. timestamp  (integer, required) Unix seconds-since-epoch timestamp\n"
            "   Pass 0 to go back to using the system time.");

    if (!Params().MineBlocksOnDemand())
        throw runtime_error("setmocktime for regression testing (-regtest mode) only");

    LOCK(cs_main);

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));
    SetMockTime(params[0].get_int64());

    return NullUniValue;
}

#ifdef ENABLE_WALLET
UniValue getstakingstatus(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getstakingstatus\n"
            "Returns an object containing various staking information.\n"
            "\nResult:\n"
            "{\n"
            "  \"validtime\": true|false,          (boolean) if the chain tip is within staking phases\n"
            "  \"haveconnections\": true|false,    (boolean) if network connections are present\n"
            "  \"walletunlocked\": true|false,     (boolean) if the wallet is unlocked\n"
            "  \"mintablecoins\": true|false,      (boolean) if the wallet has mintable coins\n"
            "  \"enoughcoins\": true|false,        (boolean) if available coins are greater than reserve balance\n"
            "  \"knsync\": true|false,             (boolean) if karmanode data is synced\n"
            "  \"staking status\": true|false,     (boolean) if the wallet is staking or not\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getstakingstatus", "") + HelpExampleRpc("getstakingstatus", ""));

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("validtime", chainActive.Tip()->nTime > 1471482000));
    obj.push_back(Pair("haveconnections", !vNodes.empty()));
    if (pwalletMain) {
        obj.push_back(Pair("walletunlocked", !pwalletMain->IsLocked()));
        obj.push_back(Pair("mintablecoins", pwalletMain->MintableCoins()));
        obj.push_back(Pair("enoughcoins", nReserveBalance <= pwalletMain->GetBalance()));
    }
    obj.push_back(Pair("knsync", karmanodeSync.IsSynced()));

    bool nStaking = false;
    if (mapHashedBlocks.count(chainActive.Tip()->nHeight))
        nStaking = true;
    else if (mapHashedBlocks.count(chainActive.Tip()->nHeight - 1) && nLastCoinStakeSearchInterval)
        nStaking = true;
    obj.push_back(Pair("staking status", nStaking));

    return obj;
}
#endif // ENABLE_WALLET
