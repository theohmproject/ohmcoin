// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The OHMC 
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activekarmanode.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "karmanode-budget.h"
#include "karmanode-payments.h"
#include "karmanodeconfig.h"
#include "karmanodeman.h"
#include "rpcserver.h"
#include "utilmoneystr.h"

#include <boost/tokenizer.hpp>

#include <fstream>
using namespace json_spirit;

void SendMoney(const CTxDestination& address, CAmount nValue, CWalletTx& wtxNew, AvailableCoinsType coin_type = ALL_COINS)
{
    // Check amount
    if (nValue <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    if (nValue > pwalletMain->GetBalance())
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

    string strError;
    if (pwalletMain->IsLocked()) {
        strError = "Error: Wallet locked, unable to create transaction!";
        LogPrintf("SendMoney() : %s", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    // Parse ohmc address
    CScript scriptPubKey = GetScriptForDestination(address);

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    if (!pwalletMain->CreateTransaction(scriptPubKey, nValue, wtxNew, reservekey, nFeeRequired, strError, NULL, coin_type)) {
        if (nValue + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        LogPrintf("SendMoney() : %s\n", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if (!pwalletMain->CommitTransaction(wtxNew, reservekey))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
}

Value privatesend(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error(
            "privatesend <ohmcaddress> <amount>\n"
            "ohmcaddress, reset, or auto (AutoDenominate)"
            "<amount> is a real and will be rounded to the next 0.1" +
            HelpRequiringPassphrase());

    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    if (params[0].get_str() == "auto") {
        if (fMasterNode)
            return "ObfuScation is not supported from karmanodes";

        return "DoAutomaticDenominating " + (obfuScationPool.DoAutomaticDenominating() ? "successful" : ("failed: " + obfuScationPool.GetStatus()));
    }

    if (params[0].get_str() == "reset") {
        obfuScationPool.Reset();
        return "successfully reset privatesend";
    }

    if (params.size() != 2)
        throw runtime_error(
            "privatesend <ohmcaddress> <amount>\n"
            "ohmcaddress, denominate, or auto (AutoDenominate)"
            "<amount> is a real and will be rounded to the next 0.1" +
            HelpRequiringPassphrase());

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid ohmc address");

    // Amount
    CAmount nAmount = AmountFromValue(params[1]);

    // Wallet comments
    CWalletTx wtx;
    //    string strError = pwalletMain->SendMoneyToDestination(address.Get(), nAmount, wtx, ONLY_DENOMINATED);
    SendMoney(address.Get(), nAmount, wtx, ONLY_DENOMINATED);
    //    if (strError != "")
    //        throw JSONRPCError(RPC_WALLET_ERROR, strError);

    return wtx.GetHash().GetHex();
}

Value getpoolinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getpoolinfo\n"
            "\nReturns anonymous pool-related information\n"

            "\nResult:\n"
            "{\n"
            "  \"current\": \"addr\",    (string) ohmc address of current karmanode\n"
            "  \"state\": xxxx,        (string) unknown\n"
            "  \"entries\": xxxx,      (numeric) Number of entries\n"
            "  \"accepted\": xxxx,     (numeric) Number of entries accepted\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getpoolinfo", "") + HelpExampleRpc("getpoolinfo", ""));

    Object obj;
    obj.push_back(Pair("current_karmanode", mnodeman.GetCurrentMasterNode()->addr.ToString()));
    obj.push_back(Pair("state", obfuScationPool.GetState()));
    obj.push_back(Pair("entries", obfuScationPool.GetEntriesCount()));
    obj.push_back(Pair("entries_accepted", obfuScationPool.GetCountEntriesAccepted()));
    return obj;
}

// This command is retained for backwards compatibility, but is depreciated.
// Future removal of this command is planned to keep things clean.
Value karmanode(const Array& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp ||
        (strCommand != "start" && strCommand != "start-alias" && strCommand != "start-many" && strCommand != "start-all" && strCommand != "start-missing" &&
            strCommand != "start-disabled" && strCommand != "list" && strCommand != "list-conf" && strCommand != "count" && strCommand != "enforce" &&
            strCommand != "debug" && strCommand != "current" && strCommand != "winners" && strCommand != "genkey" && strCommand != "connect" &&
            strCommand != "outputs" && strCommand != "status" && strCommand != "calcscore"))
        throw runtime_error(
            "karmanode \"command\"...\n"
            "\nSet of commands to execute karmanode related actions\n"
            "This command is depreciated, please see individual command documentation for future reference\n\n"

            "\nArguments:\n"
            "1. \"command\"        (string or set of strings, required) The command to execute\n"

            "\nAvailable commands:\n"
            "  count        - Print count information of all known karmanodes\n"
            "  current      - Print info on current karmanode winner\n"
            "  debug        - Print karmanode status\n"
            "  genkey       - Generate new karmanodeprivkey\n"
            "  outputs      - Print karmanode compatible outputs\n"
            "  start        - Start karmanode configured in ohmc.conf\n"
            "  start-alias  - Start single karmanode by assigned alias configured in karmanode.conf\n"
            "  start-<mode> - Start karmanodes configured in karmanode.conf (<mode>: 'all', 'missing', 'disabled')\n"
            "  status       - Print karmanode status information\n"
            "  list         - Print list of all known karmanodes (see karmanodelist for more info)\n"
            "  list-conf    - Print karmanode.conf in JSON format\n"
            "  winners      - Print list of karmanode winners\n");

    if (strCommand == "list") {
        Array newParams(params.size() - 1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return listkarmanodes(newParams, fHelp);
    }

    if (strCommand == "connect") {
        Array newParams(params.size() -1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return karmanodeconnect(newParams, fHelp);
    }

    if (strCommand == "count") {
        Array newParams(params.size() - 1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return getkarmanodecount(newParams, fHelp);
    }

    if (strCommand == "current") {
        Array newParams(params.size() - 1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return karmanodecurrent(newParams, fHelp);
    }

    if (strCommand == "debug") {
        Array newParams(params.size() - 1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return karmanodedebug(newParams, fHelp);
    }

    if (strCommand == "start" || strCommand == "start-alias" || strCommand == "start-many" || strCommand == "start-all" || strCommand == "start-missing" || strCommand == "start-disabled") {
        return startkarmanode(params, fHelp);
    }

    if (strCommand == "genkey") {
        Array newParams(params.size() - 1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return createkarmanodekey(newParams, fHelp);
    }

    if (strCommand == "list-conf") {
        Array newParams(params.size() - 1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return listkarmanodeconf(newParams, fHelp);
    }

    if (strCommand == "outputs") {
        Array newParams(params.size() - 1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return getkarmanodeoutputs(newParams, fHelp);
    }

    if (strCommand == "status") {
        Array newParams(params.size() - 1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return getkarmanodestatus(newParams, fHelp);
    }

    if (strCommand == "winners") {
        Array newParams(params.size() - 1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return getkarmanodewinners(newParams, fHelp);
    }

    if (strCommand == "calcscore") {
        Array newParams(params.size() - 1);
        std::copy(params.begin() + 1, params.end(), newParams.begin());
        return getkarmanodescores(newParams, fHelp);
    }

    return Value::null;
}

Value listkarmanodes(const Array& params, bool fHelp)
{
    std::string strFilter = "";

    if (params.size() == 1) strFilter = params[0].get_str();

    if (fHelp || (params.size() > 1))
        throw runtime_error(
            "listkarmanodes ( \"filter\" )\n"
            "\nGet a ranked list of karmanodes\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by txhash, status, or addr.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"rank\": n,           (numeric) Karmanode Rank (or 0 if not enabled)\n"
            "    \"txhash\": \"hash\",    (string) Collateral transaction hash\n"
            "    \"outidx\": n,         (numeric) Collateral transaction output index\n"
            "    \"status\": s,         (string) Status (ENABLED/EXPIRED/REMOVE/etc)\n"
            "    \"addr\": \"addr\",      (string) Karmanode ohmc address\n"
            "    \"version\": v,        (numeric) Karmanode protocol version\n"
            "    \"lastseen\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last seen\n"
            "    \"activetime\": ttt,   (numeric) The time in seconds since epoch (Jan 1 1970 GMT) karmanode has been active\n"
            "    \"lastpaid\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) karmanode was last paid\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("karmanodelist", "") + HelpExampleRpc("karmanodelist", ""));

    Array ret;
    int nHeight;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if(!pindex) return 0;
        nHeight = pindex->nHeight;
    }
    std::vector<pair<int, CKarmanode> > vKarmanodeRanks = mnodeman.GetKarmanodeRanks(nHeight);
    BOOST_FOREACH (PAIRTYPE(int, CKarmanode) & s, vKarmanodeRanks) {
        Object obj;
        std::string strVin = s.second.vin.prevout.ToStringShort();
        std::string strTxHash = s.second.vin.prevout.hash.ToString();
        uint32_t oIdx = s.second.vin.prevout.n;

        CKarmanode* mn = mnodeman.Find(s.second.vin);

        if (mn != NULL) {
            if (strFilter != "" && strTxHash.find(strFilter) == string::npos &&
                mn->Status().find(strFilter) == string::npos &&
                CBitcoinAddress(mn->pubKeyCollateralAddress.GetID()).ToString().find(strFilter) == string::npos) continue;

            std::string strStatus = mn->Status();

            obj.push_back(Pair("rank", (strStatus == "ENABLED" ? s.first : 0)));
            obj.push_back(Pair("txhash", strTxHash));
            obj.push_back(Pair("outidx", (uint64_t)oIdx));
            obj.push_back(Pair("status", strStatus));
            obj.push_back(Pair("addr", CBitcoinAddress(mn->pubKeyCollateralAddress.GetID()).ToString()));
            obj.push_back(Pair("version", mn->protocolVersion));
            obj.push_back(Pair("lastseen", (int64_t)mn->lastPing.sigTime));
            obj.push_back(Pair("activetime", (int64_t)(mn->lastPing.sigTime - mn->sigTime)));
            obj.push_back(Pair("lastpaid", (int64_t)mn->GetLastPaid()));

            ret.push_back(obj);
        }
    }

    return ret;
}

Value karmanodeconnect(const Array& params, bool fHelp)
{
    if (fHelp || (params.size() != 1))
        throw runtime_error(
            "karmanodeconnect \"address\"\n"
            "\nAttempts to connect to specified karmanode address\n"

            "\nArguments:\n"
            "1. \"address\"     (string, required) IP or net address to connect to\n"

            "\nExamples:\n" +
            HelpExampleCli("karmanodeconnect", "\"192.168.0.6:51472\"") + HelpExampleRpc("karmanodeconnect", "\"192.168.0.6:51472\""));

    std::string strAddress = params[0].get_str();

    CService addr = CService(strAddress);

    CNode* pnode = ConnectNode((CAddress)addr, NULL, false);
    if (pnode) {
        pnode->Release();
        return Value::null;
    } else {
        throw runtime_error("error connecting\n");
    }
}

Value getkarmanodecount (const Array& params, bool fHelp)
{
    if (fHelp || (params.size() > 0))
        throw runtime_error(
            "getkarmanodecount\n"
            "\nGet karmanode count values\n"

            "\nResult:\n"
            "{\n"
            "  \"total\": n,        (numeric) Total karmanodes\n"
            "  \"stable\": n,       (numeric) Stable count\n"
            "  \"obfcompat\": n,    (numeric) PrivateSend Compatible\n"
            "  \"enabled\": n,      (numeric) Enabled karmanodes\n"
            "  \"inqueue\": n       (numeric) Karmanodes in queue\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getkarmanodecount", "") + HelpExampleRpc("getkarmanodecount", ""));

    Object obj;
    int nCount = 0;

    if (chainActive.Tip())
        mnodeman.GetNextKarmanodeInQueueForPayment(chainActive.Tip()->nHeight, true, nCount);

    obj.push_back(Pair("total", mnodeman.size()));
    obj.push_back(Pair("stable", mnodeman.stable_size()));
    obj.push_back(Pair("obfcompat", mnodeman.CountEnabled(ActiveProtocol())));
    obj.push_back(Pair("enabled", mnodeman.CountEnabled()));
    obj.push_back(Pair("inqueue", nCount));

    return obj;
}

Value karmanodecurrent (const Array& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "karmanodecurrent\n"
            "\nGet current karmanode winner\n"

            "\nResult:\n"
            "{\n"
            "  \"protocol\": xxxx,        (numeric) Protocol version\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"pubkey\": \"xxxx\",      (string) MN Public key\n"
            "  \"lastseen\": xxx,       (numeric) Time since epoch of last seen\n"
            "  \"activeseconds\": xxx,  (numeric) Seconds MN has been active\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("karmanodecurrent", "") + HelpExampleRpc("karmanodecurrent", ""));

    CKarmanode* winner = mnodeman.GetCurrentMasterNode(1);
    if (winner) {
        Object obj;

        obj.push_back(Pair("protocol", (int64_t)winner->protocolVersion));
        obj.push_back(Pair("txhash", winner->vin.prevout.hash.ToString()));
        obj.push_back(Pair("pubkey", CBitcoinAddress(winner->pubKeyCollateralAddress.GetID()).ToString()));
        obj.push_back(Pair("lastseen", (winner->lastPing == CKarmanodePing()) ? winner->sigTime : (int64_t)winner->lastPing.sigTime));
        obj.push_back(Pair("activeseconds", (winner->lastPing == CKarmanodePing()) ? 0 : (int64_t)(winner->lastPing.sigTime - winner->sigTime)));
        return obj;
    }

    throw runtime_error("unknown");
}

Value karmanodedebug (const Array& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "karmanodedebug\n"
            "\nPrint karmanode status\n"

            "\nResult:\n"
            "\"status\"     (string) Karmanode status message\n"
            "\nExamples:\n" +
            HelpExampleCli("karmanodedebug", "") + HelpExampleRpc("karmanodedebug", ""));

    if (activeKarmanode.status != ACTIVE_KARMANODE_INITIAL || !karmanodeSync.IsSynced())
        return activeKarmanode.GetStatus();

    CTxIn vin = CTxIn();
    CPubKey pubkey = CScript();
    CKey key;
    if (!activeKarmanode.GetMasterNodeVin(vin, pubkey, key))
        throw runtime_error("Missing karmanode input, please look at the documentation for instructions on karmanode creation\n");
    else
        return activeKarmanode.GetStatus();
}

Value startkarmanode (const Array& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();

        // Backwards compatibility with legacy 'karmanode' super-command forwarder
        if (strCommand == "start") strCommand = "local";
        if (strCommand == "start-alias") strCommand = "alias";
        if (strCommand == "start-all") strCommand = "all";
        if (strCommand == "start-many") strCommand = "many";
        if (strCommand == "start-missing") strCommand = "missing";
        if (strCommand == "start-disabled") strCommand = "disabled";
    }

    if (fHelp || params.size() < 2 || params.size() > 3 ||
        (params.size() == 2 && (strCommand != "local" && strCommand != "all" && strCommand != "many" && strCommand != "missing" && strCommand != "disabled")) ||
        (params.size() == 3 && strCommand != "alias"))
        throw runtime_error(
            "startkarmanode \"local|all|many|missing|disabled|alias\" lockwallet ( \"alias\" )\n"
            "\nAttempts to start one or more karmanode(s)\n"

            "\nArguments:\n"
            "1. set         (string, required) Specify which set of karmanode(s) to start.\n"
            "2. lockwallet  (boolean, required) Lock wallet after completion.\n"
            "3. alias       (string) Karmanode alias. Required if using 'alias' as the set.\n"

            "\nResult: (for 'local' set):\n"
            "\"status\"     (string) Karmanode status message\n"

            "\nResult: (for other sets):\n"
            "{\n"
            "  \"overall\": \"xxxx\",     (string) Overall status message\n"
            "  \"detail\": [\n"
            "    {\n"
            "      \"node\": \"xxxx\",    (string) Node name or alias\n"
            "      \"result\": \"xxxx\",  (string) 'success' or 'failed'\n"
            "      \"error\": \"xxxx\"    (string) Error message, if failed\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("karmanodestart", "\"alias\" \"my_mn\"") + HelpExampleRpc("karmanodestart", "\"alias\" \"my_mn\""));

    bool fLock = (params[1].get_str() == "true" ? true : false);

    if (strCommand == "local") {
        if (!fMasterNode) throw runtime_error("you must set karmanode=1 in the configuration\n");

        if (pwalletMain->IsLocked())
            throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");

        if (activeKarmanode.status != ACTIVE_KARMANODE_STARTED) {
            activeKarmanode.status = ACTIVE_KARMANODE_INITIAL; // TODO: consider better way
            activeKarmanode.ManageStatus();
            if (fLock)
                pwalletMain->Lock();
        }

        return activeKarmanode.GetStatus();
    }

    if (strCommand == "all" || strCommand == "many" || strCommand == "missing" || strCommand == "disabled") {
        if (pwalletMain->IsLocked())
            throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");

        if ((strCommand == "missing" || strCommand == "disabled") &&
            (karmanodeSync.RequestedKarmanodeAssets <= KARMANODE_SYNC_LIST ||
                karmanodeSync.RequestedKarmanodeAssets == KARMANODE_SYNC_FAILED)) {
            throw runtime_error("You can't use this command until karmanode list is synced\n");
        }

        std::vector<CKarmanodeConfig::CKarmanodeEntry> mnEntries;
        mnEntries = karmanodeConfig.getEntries();

        int successful = 0;
        int failed = 0;

        Array resultsObj;

        BOOST_FOREACH (CKarmanodeConfig::CKarmanodeEntry mne, karmanodeConfig.getEntries()) {
            std::string errorMessage;
            int nIndex;
            if(!mne.castOutputIndex(nIndex))
                continue;
            CTxIn vin = CTxIn(uint256(mne.getTxHash()), uint32_t(nIndex));
            CKarmanode* pmn = mnodeman.Find(vin);

            if (pmn != NULL) {
                if (strCommand == "missing") continue;
                if (strCommand == "disabled" && pmn->IsEnabled()) continue;
            }

            bool result = activeKarmanode.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage);

            Object statusObj;
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("result", result ? "success" : "failed"));

            if (result) {
                successful++;
                statusObj.push_back(Pair("error", ""));
            } else {
                failed++;
                statusObj.push_back(Pair("error", errorMessage));
            }

            resultsObj.push_back(statusObj);
        }
        if (fLock)
            pwalletMain->Lock();

        Object returnObj;
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d karmanodes, failed to start %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if (strCommand == "alias") {
        std::string alias = params[2].get_str();

        if (pwalletMain->IsLocked())
            throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");

        bool found = false;
        int successful = 0;
        int failed = 0;

        Array resultsObj;
        Object statusObj;
        statusObj.push_back(Pair("alias", alias));

        BOOST_FOREACH (CKarmanodeConfig::CKarmanodeEntry mne, karmanodeConfig.getEntries()) {
            if (mne.getAlias() == alias) {
                found = true;
                std::string errorMessage;

                bool result = activeKarmanode.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage);

                statusObj.push_back(Pair("result", result ? "successful" : "failed"));

                if (result) {
                    successful++;
                    statusObj.push_back(Pair("error", ""));
                } else {
                    failed++;
                    statusObj.push_back(Pair("error", errorMessage));
                }
                break;
            }
        }

        if (!found) {
            failed++;
            statusObj.push_back(Pair("result", "failed"));
            statusObj.push_back(Pair("error", "could not find alias in config. Verify with list-conf."));
        }

        resultsObj.push_back(statusObj);

        if (fLock)
            pwalletMain->Lock();

        Object returnObj;
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d karmanodes, failed to start %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }
    return Value::null;
}

Value createkarmanodekey (const Array& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "createkarmanodekey\n"
            "\nCreate a new karmanode private key\n"

            "\nResult:\n"
            "\"key\"    (string) Karmanode private key\n"
            "\nExamples:\n" +
            HelpExampleCli("createkarmanodekey", "") + HelpExampleRpc("createkarmanodekey", ""));

    CKey secret;
    secret.MakeNewKey(false);

    return CBitcoinSecret(secret).ToString();
}

Value getkarmanodeoutputs (const Array& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "getkarmanodeoutputs\n"
            "\nPrint all karmanode transaction outputs\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txhash\": \"xxxx\",    (string) output transaction hash\n"
            "    \"outputidx\": n       (numeric) output index number\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getkarmanodeoutputs", "") + HelpExampleRpc("getkarmanodeoutputs", ""));

    // Find possible candidates
    vector<COutput> possibleCoins = activeKarmanode.SelectCoinsKarmanode();

    Array ret;
    BOOST_FOREACH (COutput& out, possibleCoins) {
        Object obj;
        obj.push_back(Pair("txhash", out.tx->GetHash().ToString()));
        obj.push_back(Pair("outputidx", out.i));
        ret.push_back(obj);
    }

    return ret;
}

Value listkarmanodeconf (const Array& params, bool fHelp)
{
    std::string strFilter = "";

    if (params.size() == 1) strFilter = params[0].get_str();

    if (fHelp || (params.size() > 1))
        throw runtime_error(
            "listkarmanodeconf ( \"filter\" )\n"
            "\nPrint karmanode.conf in JSON format\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match on alias, address, txHash, or status.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"alias\": \"xxxx\",        (string) karmanode alias\n"
            "    \"address\": \"xxxx\",      (string) karmanode IP address\n"
            "    \"privateKey\": \"xxxx\",   (string) karmanode private key\n"
            "    \"txHash\": \"xxxx\",       (string) transaction hash\n"
            "    \"outputIndex\": n,       (numeric) transaction output index\n"
            "    \"status\": \"xxxx\"        (string) karmanode status\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listkarmanodeconf", "") + HelpExampleRpc("listkarmanodeconf", ""));

    std::vector<CKarmanodeConfig::CKarmanodeEntry> mnEntries;
    mnEntries = karmanodeConfig.getEntries();

    Array ret;

    BOOST_FOREACH (CKarmanodeConfig::CKarmanodeEntry mne, karmanodeConfig.getEntries()) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;
        CTxIn vin = CTxIn(uint256(mne.getTxHash()), uint32_t(nIndex));
        CKarmanode* pmn = mnodeman.Find(vin);

        std::string strStatus = pmn ? pmn->Status() : "MISSING";

        if (strFilter != "" && mne.getAlias().find(strFilter) == string::npos &&
            mne.getIp().find(strFilter) == string::npos &&
            mne.getTxHash().find(strFilter) == string::npos &&
            strStatus.find(strFilter) == string::npos) continue;

        Object mnObj;
        mnObj.push_back(Pair("alias", mne.getAlias()));
        mnObj.push_back(Pair("address", mne.getIp()));
        mnObj.push_back(Pair("privateKey", mne.getPrivKey()));
        mnObj.push_back(Pair("txHash", mne.getTxHash()));
        mnObj.push_back(Pair("outputIndex", mne.getOutputIndex()));
        mnObj.push_back(Pair("status", strStatus));
        ret.push_back(mnObj);
    }

    return ret;
}

Value getkarmanodestatus (const Array& params, bool fHelp)
{
    if (fHelp || (params.size() != 0))
        throw runtime_error(
            "getkarmanodestatus\n"
            "\nPrint karmanode status\n"

            "\nResult:\n"
            "{\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"outputidx\": n,        (numeric) Collateral transaction output index number\n"
            "  \"netaddr\": \"xxxx\",     (string) Karmanode network address\n"
            "  \"addr\": \"xxxx\",        (string) ohmc address for karmanode payments\n"
            "  \"status\": \"xxxx\",      (string) Karmanode status\n"
            "  \"message\": \"xxxx\"      (string) Karmanode status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getkarmanodestatus", "") + HelpExampleRpc("getkarmanodestatus", ""));

    if (!fMasterNode) throw runtime_error("This is not a karmanode");

    CKarmanode* pmn = mnodeman.Find(activeKarmanode.vin);

    if (pmn) {
        Object mnObj;
        mnObj.push_back(Pair("txhash", activeKarmanode.vin.prevout.hash.ToString()));
        mnObj.push_back(Pair("outputidx", (uint64_t)activeKarmanode.vin.prevout.n));
        mnObj.push_back(Pair("netaddr", activeKarmanode.service.ToString()));
        mnObj.push_back(Pair("addr", CBitcoinAddress(pmn->pubKeyCollateralAddress.GetID()).ToString()));
        mnObj.push_back(Pair("status", activeKarmanode.status));
        mnObj.push_back(Pair("message", activeKarmanode.GetStatus()));
        return mnObj;
    }
    throw runtime_error("Karmanode not found in the list of available karmanodes. Current status: "
                        + activeKarmanode.GetStatus());
}

Value getkarmanodewinners (const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
            "getkarmanodewinners ( blocks \"filter\" )\n"
            "\nPrint the karmanode winners for the last n blocks\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Number of previous blocks to show (default: 10)\n"
            "2. filter      (string, optional) Search filter matching MN address\n"

            "\nResult (single winner):\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": {\n"
            "      \"address\": \"xxxx\",    (string) ohmc MN Address\n"
            "      \"nVotes\": n,          (numeric) Number of votes for winner\n"
            "    }\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nResult (multiple winners):\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": [\n"
            "      {\n"
            "        \"address\": \"xxxx\",  (string) ohmc MN Address\n"
            "        \"nVotes\": n,        (numeric) Number of votes for winner\n"
            "      }\n"
            "      ,...\n"
            "    ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getkarmanodewinners", "") + HelpExampleRpc("getkarmanodewinners", ""));

    int nHeight;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if(!pindex) return 0;
        nHeight = pindex->nHeight;
    }

    int nLast = 10;
    std::string strFilter = "";

    if (params.size() >= 1)
        nLast = atoi(params[0].get_str());

    if (params.size() == 2)
        strFilter = params[1].get_str();

    Array ret;

    for (int i = nHeight - nLast; i < nHeight + 20; i++) {
        Object obj;
        obj.push_back(Pair("nHeight", i));

        std::string strPayment = GetRequiredPaymentsString(i);
        if (strFilter != "" && strPayment.find(strFilter) == std::string::npos) continue;

        if (strPayment.find(',') != std::string::npos) {
            Array winner;
            boost::char_separator<char> sep(",");
            boost::tokenizer< boost::char_separator<char> > tokens(strPayment, sep);
            BOOST_FOREACH (const string& t, tokens) {
                Object addr;
                std::size_t pos = t.find(":");
                std::string strAddress = t.substr(0,pos);
                uint64_t nVotes = atoi(t.substr(pos+1));
                addr.push_back(Pair("address", strAddress));
                addr.push_back(Pair("nVotes", nVotes));
                winner.push_back(addr);
            }
            obj.push_back(Pair("winner", winner));
        } else if (strPayment.find("Unknown") == std::string::npos) {
            Object winner;
            std::size_t pos = strPayment.find(":");
            std::string strAddress = strPayment.substr(0,pos);
            uint64_t nVotes = atoi(strPayment.substr(pos+1));
            winner.push_back(Pair("address", strAddress));
            winner.push_back(Pair("nVotes", nVotes));
            obj.push_back(Pair("winner", winner));
        } else {
            Object winner;
            winner.push_back(Pair("address", strPayment));
            winner.push_back(Pair("nVotes", 0));
            obj.push_back(Pair("winner", winner));
        }

            ret.push_back(obj);
    }

    return ret;
}

Value getkarmanodescores (const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getkarmanodescores ( blocks )\n"
            "\nPrint list of winning karmanode by score\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Show the last n blocks (default 10)\n"

            "\nResult:\n"
            "{\n"
            "  xxxx: \"xxxx\"   (numeric : string) Block height : Karmanode hash\n"
            "  ,...\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getkarmanodescores", "") + HelpExampleRpc("getkarmanodescores", ""));

    int nLast = 10;

    if (params.size() == 1) {
        try {
            nLast = std::stoi(params[0].get_str());
        } catch (const boost::bad_lexical_cast &) {
            throw runtime_error("Exception on param 2");
        }
    }
    Object obj;

    std::vector<CKarmanode> vKarmanodes = mnodeman.GetFullKarmanodeVector();
    for (int nHeight = chainActive.Tip()->nHeight - nLast; nHeight < chainActive.Tip()->nHeight + 20; nHeight++) {
        uint256 nHigh = 0;
        CKarmanode* pBestKarmanode = NULL;
        BOOST_FOREACH (CKarmanode& mn, vKarmanodes) {
            uint256 n = mn.CalculateScore(1, nHeight - 100);
            if (n > nHigh) {
                nHigh = n;
                pBestKarmanode = &mn;
            }
        }
        if (pBestKarmanode)
            obj.push_back(Pair(strprintf("%d", nHeight), pBestKarmanode->vin.prevout.hash.ToString().c_str()));
    }

    return obj;
}