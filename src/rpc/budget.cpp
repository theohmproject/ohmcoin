// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activekarmanode.h"
#include "wallet/db.h"
#include "init.h"
#include "main.h"
#include "karmanode-budget.h"
#include "karmanode-payments.h"
#include "karmanodeconfig.h"
#include "karmanodeman.h"
#include "rpc/server.h"
#include "utilmoneystr.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <univalue.h>

#include <fstream>
using namespace std;

void budgetToJSON(CBudgetProposal* pbudgetProposal, UniValue& bObj)
{
    CTxDestination address;
    ExtractDestination(pbudgetProposal->GetPayee(), address);

    bObj.push_back(Pair("Name", pbudgetProposal->GetName()));
    bObj.push_back(Pair("URL", pbudgetProposal->GetURL()));
    bObj.push_back(Pair("Hash", pbudgetProposal->GetHash().ToString()));
    bObj.push_back(Pair("FeeHash", pbudgetProposal->nFeeTXHash.ToString()));
    bObj.push_back(Pair("BlockStart", (int64_t)pbudgetProposal->GetBlockStart()));
    bObj.push_back(Pair("BlockEnd", (int64_t)pbudgetProposal->GetBlockEnd()));
    bObj.push_back(Pair("TotalPaymentCount", (int64_t)pbudgetProposal->GetTotalPaymentCount()));
    bObj.push_back(Pair("RemainingPaymentCount", (int64_t)pbudgetProposal->GetRemainingPaymentCount()));
    bObj.push_back(Pair("PaymentAddress", EncodeDestination(address)));
    bObj.push_back(Pair("Ratio", pbudgetProposal->GetRatio()));
    bObj.push_back(Pair("Yeas", (int64_t)pbudgetProposal->GetYeas()));
    bObj.push_back(Pair("Nays", (int64_t)pbudgetProposal->GetNays()));
    bObj.push_back(Pair("Abstains", (int64_t)pbudgetProposal->GetAbstains()));
    bObj.push_back(Pair("TotalPayment", ValueFromAmount(pbudgetProposal->GetAmount() * pbudgetProposal->GetTotalPaymentCount())));
    bObj.push_back(Pair("MonthlyPayment", ValueFromAmount(pbudgetProposal->GetAmount())));
    bObj.push_back(Pair("IsEstablished", pbudgetProposal->IsEstablished()));

    std::string strError = "";
    bObj.push_back(Pair("IsValid", pbudgetProposal->IsValid(strError)));
    bObj.push_back(Pair("IsValidReason", strError.c_str()));
    bObj.push_back(Pair("fValid", pbudgetProposal->fValid));
}

// This command is retained for backwards compatibility, but is deprecated.
// Future removal of this command is planned to keep things clean.
UniValue knbudget(const UniValue& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp ||
        (strCommand != "vote-alias" && strCommand != "vote-many" && strCommand != "prepare" && strCommand != "submit" && strCommand != "vote" && strCommand != "getvotes" && strCommand != "getinfo" && strCommand != "show" && strCommand != "projection" && strCommand != "check" && strCommand != "nextblock"))
        throw runtime_error(
            "knbudget \"command\"... ( \"passphrase\" )\n"
            "\nVote or show current budgets\n"
            "This command is deprecated, please see individual command documentation for future reference\n\n"

            "\nAvailable commands:\n"
            "  prepare            - Prepare proposal for network by signing and creating tx\n"
            "  submit             - Submit proposal for network\n"
            "  vote-many          - Vote on a Ohmcoin initiative\n"
            "  vote-alias         - Vote on a Ohmcoin initiative\n"
            "  vote               - Vote on a Ohmcoin initiative/budget\n"
            "  getvotes           - Show current karmanode budgets\n"
            "  getinfo            - Show current karmanode budgets\n"
            "  show               - Show all budgets\n"
            "  projection         - Show the projection of which proposals will be paid the next cycle\n"
            "  check              - Scan proposals and remove invalid\n"
            "  nextblock          - Get next superblock for budget system\n");

    if (strCommand == "nextblock") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getnextsuperblock(newParams, fHelp);
    }

    if (strCommand == "prepare") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return preparebudget(newParams, fHelp);
    }

    if (strCommand == "submit") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return submitbudget(newParams, fHelp);
    }

    if (strCommand == "vote" || strCommand == "vote-many" || strCommand == "vote-alias") {
        if (strCommand == "vote-alias")
            throw runtime_error(
                "vote-alias is not supported with this command\n"
                "Please use knbudgetvote instead.\n"
            );
        return knbudgetvote(params, fHelp);
    }

    if (strCommand == "projection") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getbudgetprojection(newParams, fHelp);
    }

    if (strCommand == "show" || strCommand == "getinfo") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getbudgetinfo(newParams, fHelp);
    }

    if (strCommand == "getvotes") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return getbudgetvotes(newParams, fHelp);
    }

    if (strCommand == "check") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip command
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return checkbudgets(newParams, fHelp);
    }

    return NullUniValue;
}

UniValue preparebudget(const UniValue& params, bool fHelp)
{
    int nBlockMin = 0;
    CBlockIndex* pindexPrev = chainActive.Tip();

    if (fHelp || params.size() != 6)
        throw runtime_error(
            "preparebudget \"proposal-name\" \"url\" payment-count block-start \"ohmcoin-address\" monthly-payment\n"
            "\nPrepare proposal for network by signing and creating tx\n"

            "\nArguments:\n"
            "1. \"proposal-name\":  (string, required) Desired proposal name (20 character limit)\n"
            "2. \"url\":            (string, required) URL of proposal details (64 character limit)\n"
            "3. payment-count:    (numeric, required) Total number of monthly payments\n"
            "4. block-start:      (numeric, required) Starting super block height\n"
            "5. \"ohmcoin-address\":   (string, required) Ohmcoin address to send payments to\n"
            "6. monthly-payment:  (numeric, required) Monthly payment amount\n"

            "\nResult:\n"
            "\"xxxx\"       (string) proposal fee hash (if successful) or error message (if failed)\n"
            "\nExamples:\n" +
            HelpExampleCli("preparebudget", "\"test-proposal\" \"https://forum.ohmcoin.org/t/test-proposal\" 2 820800 \"D9oc6C3dttUbv8zd7zGNq1qKBGf4ZQ1XEE\" 500") +
            HelpExampleRpc("preparebudget", "\"test-proposal\" \"https://forum.ohmcoin.org/t/test-proposal\" 2 820800 \"D9oc6C3dttUbv8zd7zGNq1qKBGf4ZQ1XEE\" 500"));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    std::string strProposalName = SanitizeString(params[0].get_str());
    if (strProposalName.size() > 20)
        throw runtime_error("Invalid proposal name, limit of 20 characters.");

    std::string strURL = SanitizeString(params[1].get_str());
    if (strURL.size() > 64)
        throw runtime_error("Invalid url, limit of 64 characters.");

    int nPaymentCount = params[2].get_int();
    if (nPaymentCount < 1)
        throw runtime_error("Invalid payment count, must be more than zero.");

    // Start must be in the next budget cycle
    if (pindexPrev != NULL) nBlockMin = pindexPrev->nHeight - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks() + GetBudgetPaymentCycleBlocks();

    int nBlockStart = params[3].get_int();
    if (nBlockStart % GetBudgetPaymentCycleBlocks() != 0) {
        int nNext = pindexPrev->nHeight - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks() + GetBudgetPaymentCycleBlocks();
        throw runtime_error(strprintf("Invalid block start - must be a budget cycle block. Next valid block: %d", nNext));
    }

    int nBlockEnd = nBlockStart + GetBudgetPaymentCycleBlocks() * nPaymentCount; // End must be AFTER current cycle

    if (nBlockStart < nBlockMin)
        throw runtime_error("Invalid block start, must be more than current height.");

    if (nBlockEnd < pindexPrev->nHeight)
        throw runtime_error("Invalid ending block, starting block + (payment_cycle*payments) must be more than current height.");


    if (!IsValidDestinationString(params[4].get_str()))

        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Ohmcoin address");

    CTxDestination address = DecodeDestination(params[4].get_str());

    // Parse Ohmcoin address
    CScript scriptPubKey = GetScriptForDestination(address);
    CAmount nAmount = AmountFromValue(params[5]);

    //*************************************************************************

    // create transaction 15 minutes into the future, to allow for confirmation time
    CBudgetProposalBroadcast budgetProposalBroadcast(strProposalName, strURL, nPaymentCount, scriptPubKey, nAmount, nBlockStart, 0);

    std::string strError = "";
    if (!budgetProposalBroadcast.IsValid(strError, false))
        throw runtime_error("Proposal is not valid - " + budgetProposalBroadcast.GetHash().ToString() + " - " + strError);

    bool useIX = false; //true;
    // if (params.size() > 7) {
    //     if(params[7].get_str() != "false" && params[7].get_str() != "true")
    //         return "Invalid use_ix, must be true or false";
    //     useIX = params[7].get_str() == "true" ? true : false;
    // }

    CWalletTx wtx;
    if (!pwalletMain->GetBudgetSystemCollateralTX(wtx, budgetProposalBroadcast.GetHash(), useIX)) {
        throw runtime_error("Error making collateral transaction for proposal. Please check your wallet balance.");
    }

    // make our change address
    CReserveKey reservekey(pwalletMain);
    //send the tx to the network
    pwalletMain->CommitTransaction(wtx, reservekey, useIX ? NetMsgType::IX : NetMsgType::TX);

    return wtx.GetHash().ToString();
}

UniValue submitbudget(const UniValue& params, bool fHelp)
{
    int nBlockMin = 0;
    CBlockIndex* pindexPrev = chainActive.Tip();

    if (fHelp || params.size() != 7)
        throw runtime_error(
            "submitbudget \"proposal-name\" \"url\" payment-count block-start \"ohmcoin-address\" monthly-payment \"fee-tx\"\n"
            "\nSubmit proposal to the network\n"

            "\nArguments:\n"
            "1. \"proposal-name\":  (string, required) Desired proposal name (20 character limit)\n"
            "2. \"url\":            (string, required) URL of proposal details (64 character limit)\n"
            "3. payment-count:    (numeric, required) Total number of monthly payments\n"
            "4. block-start:      (numeric, required) Starting super block height\n"
            "5. \"ohmcoin-address\":   (string, required) Ohmcoin address to send payments to\n"
            "6. monthly-payment:  (numeric, required) Monthly payment amount\n"
            "7. \"fee-tx\":         (string, required) Transaction hash from preparebudget command\n"

            "\nResult:\n"
            "\"xxxx\"       (string) proposal hash (if successful) or error message (if failed)\n"
            "\nExamples:\n" +
            HelpExampleCli("submitbudget", "\"test-proposal\" \"https://forum.ohmcoin.org/t/test-proposal\" 2 820800 \"D9oc6C3dttUbv8zd7zGNq1qKBGf4ZQ1XEE\" 500") +
            HelpExampleRpc("submitbudget", "\"test-proposal\" \"https://forum.ohmcoin.org/t/test-proposal\" 2 820800 \"D9oc6C3dttUbv8zd7zGNq1qKBGf4ZQ1XEE\" 500"));

    // Check these inputs the same way we check the vote commands:
    // **********************************************************

    std::string strProposalName = SanitizeString(params[0].get_str());
    if (strProposalName.size() > 20)
        throw runtime_error("Invalid proposal name, limit of 20 characters.");

    std::string strURL = SanitizeString(params[1].get_str());
    if (strURL.size() > 64)
        throw runtime_error("Invalid url, limit of 64 characters.");

    int nPaymentCount = params[2].get_int();
    if (nPaymentCount < 1)
        throw runtime_error("Invalid payment count, must be more than zero.");

    // Start must be in the next budget cycle
    if (pindexPrev != NULL) nBlockMin = pindexPrev->nHeight - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks() + GetBudgetPaymentCycleBlocks();

    int nBlockStart = params[3].get_int();
    if (nBlockStart % GetBudgetPaymentCycleBlocks() != 0) {
        int nNext = pindexPrev->nHeight - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks() + GetBudgetPaymentCycleBlocks();
        throw runtime_error(strprintf("Invalid block start - must be a budget cycle block. Next valid block: %d", nNext));
    }

    int nBlockEnd = nBlockStart + (GetBudgetPaymentCycleBlocks() * nPaymentCount); // End must be AFTER current cycle

    if (nBlockStart < nBlockMin)
        throw runtime_error("Invalid block start, must be more than current height.");

    if (nBlockEnd < pindexPrev->nHeight)
        throw runtime_error("Invalid ending block, starting block + (payment_cycle*payments) must be more than current height.");

    if (!IsValidDestinationString(params[4].get_str()))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Ohmcoin address");

    CTxDestination address = DecodeDestination(params[4].get_str());

    // Parse Ohmcoin address
    CScript scriptPubKey = GetScriptForDestination(address);
    CAmount nAmount = AmountFromValue(params[5]);
    uint256 hash = ParseHashV(params[6], "parameter 1");

    //create the proposal incase we're the first to make it
    CBudgetProposalBroadcast budgetProposalBroadcast(strProposalName, strURL, nPaymentCount, scriptPubKey, nAmount, nBlockStart, hash);

    std::string strError = "";
    int nConf = 0;
    if (!IsBudgetCollateralValid(hash, budgetProposalBroadcast.GetHash(), strError, budgetProposalBroadcast.nTime, nConf)) {
        throw runtime_error("Proposal FeeTX is not valid - " + hash.ToString() + " - " + strError);
    }

    if (!karmanodeSync.IsBlockchainSynced()) {
        throw runtime_error("Must wait for client to sync with karmanode network. Try again in a minute or so.");
    }

    // if(!budgetProposalBroadcast.IsValid(strError)){
    //     return "Proposal is not valid - " + budgetProposalBroadcast.GetHash().ToString() + " - " + strError;
    // }

    budget.mapSeenKarmanodeBudgetProposals.insert(make_pair(budgetProposalBroadcast.GetHash(), budgetProposalBroadcast));
    budgetProposalBroadcast.Relay();
    if(budget.AddProposal(budgetProposalBroadcast)) {
        return budgetProposalBroadcast.GetHash().ToString();
    }
    throw runtime_error("Invalid proposal, see debug.log for details.");
}

UniValue knbudgetvote(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();

        // Backwards compatibility with legacy `knbudget` command
        if (strCommand == "vote") strCommand = "local";
        if (strCommand == "vote-many") strCommand = "many";
        if (strCommand == "vote-alias") strCommand = "alias";
    }

    if (fHelp || (params.size() == 3 && (strCommand != "local" && strCommand != "many")) || (params.size() == 4 && strCommand != "alias") ||
        params.size() > 4 || params.size() < 3)
        throw runtime_error(
            "knbudgetvote \"local|many|alias\" \"votehash\" \"yes|no\" ( \"alias\" )\n"
            "\nVote on a budget proposal\n"

            "\nArguments:\n"
            "1. \"mode\"      (string, required) The voting mode. 'local' for voting directly from a karmanode, 'many' for voting with a MN controller and casting the same vote for each MN, 'alias' for voting with a MN controller and casting a vote for a single MN\n"
            "2. \"votehash\"  (string, required) The vote hash for the proposal\n"
            "3. \"votecast\"  (string, required) Your vote. 'yes' to vote for the proposal, 'no' to vote against\n"
            "4. \"alias\"     (string, required for 'alias' mode) The MN alias to cast a vote for.\n"

            "\nResult:\n"
            "{\n"
            "  \"overall\": \"xxxx\",      (string) The overall status message for the vote cast\n"
            "  \"detail\": [\n"
            "    {\n"
            "      \"node\": \"xxxx\",      (string) 'local' or the MN alias\n"
            "      \"result\": \"xxxx\",    (string) Either 'Success' or 'Failed'\n"
            "      \"error\": \"xxxx\",     (string) Error message, if vote failed\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("knbudgetvote", "\"local\" \"ed2f83cedee59a91406f5f47ec4d60bf5a7f9ee6293913c82976bd2d3a658041\" \"yes\"") +
            HelpExampleRpc("knbudgetvote", "\"local\" \"ed2f83cedee59a91406f5f47ec4d60bf5a7f9ee6293913c82976bd2d3a658041\" \"yes\""));

    uint256 hash = ParseHashV(params[1], "parameter 1");
    std::string strVote = params[2].get_str();

    if (strVote != "yes" && strVote != "no") return "You can only vote 'yes' or 'no'";
    int nVote = VOTE_ABSTAIN;
    if (strVote == "yes") nVote = VOTE_YES;
    if (strVote == "no") nVote = VOTE_NO;

    int success = 0;
    int failed = 0;

    UniValue resultsObj(UniValue::VARR);

    if (strCommand == "local") {
        CPubKey pubKeyKarmanode;
        CKey keyKarmanode;
        std::string errorMessage;

        UniValue statusObj(UniValue::VOBJ);

        while (true) {
            if (!obfuScationSigner.SetKey(strMasterNodePrivKey, errorMessage, keyKarmanode, pubKeyKarmanode)) {
                failed++;
                statusObj.push_back(Pair("node", "local"));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Karmanode signing error, could not set key correctly: " + errorMessage));
                resultsObj.push_back(statusObj);
                break;
            }

            CKarmanode* pmn = mnodeman.Find(activeKarmanode.vin);
            if (pmn == NULL) {
                failed++;
                statusObj.push_back(Pair("node", "local"));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Failure to find karmanode in list : " + activeKarmanode.vin.ToString()));
                resultsObj.push_back(statusObj);
                break;
            }

            CBudgetVote vote(activeKarmanode.vin, hash, nVote);
            if (!vote.Sign(keyKarmanode, pubKeyKarmanode)) {
                failed++;
                statusObj.push_back(Pair("node", "local"));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Failure to sign."));
                resultsObj.push_back(statusObj);
                break;
            }

            std::string strError = "";
            if (budget.UpdateProposal(vote, NULL, strError)) {
                success++;
                budget.mapSeenKarmanodeBudgetVotes.insert(make_pair(vote.GetHash(), vote));
                vote.Relay();
                statusObj.push_back(Pair("node", "local"));
                statusObj.push_back(Pair("result", "success"));
                statusObj.push_back(Pair("error", ""));
            } else {
                failed++;
                statusObj.push_back(Pair("node", "local"));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Error voting : " + strError));
            }
            resultsObj.push_back(statusObj);
            break;
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if (strCommand == "many") {
        for (CKarmanodeConfig::CKarmanodeEntry mne : karmanodeConfig.getEntries()) {
            std::string errorMessage;
            std::vector<unsigned char> vchMasterNodeSignature;
            std::string strMasterNodeSignMessage;

            CPubKey pubKeyCollateralAddress;
            CKey keyCollateralAddress;
            CPubKey pubKeyKarmanode;
            CKey keyKarmanode;

            UniValue statusObj(UniValue::VOBJ);

            if (!obfuScationSigner.SetKey(mne.getPrivKey(), errorMessage, keyKarmanode, pubKeyKarmanode)) {
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Karmanode signing error, could not set key correctly: " + errorMessage));
                resultsObj.push_back(statusObj);
                continue;
            }

            CKarmanode* pmn = mnodeman.Find(pubKeyKarmanode);
            if (pmn == NULL) {
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Can't find karmanode by pubkey"));
                resultsObj.push_back(statusObj);
                continue;
            }

            CBudgetVote vote(pmn->vin, hash, nVote);
            if (!vote.Sign(keyKarmanode, pubKeyKarmanode)) {
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Failure to sign."));
                resultsObj.push_back(statusObj);
                continue;
            }

            std::string strError = "";
            if (budget.UpdateProposal(vote, NULL, strError)) {
                budget.mapSeenKarmanodeBudgetVotes.insert(make_pair(vote.GetHash(), vote));
                vote.Relay();
                success++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "success"));
                statusObj.push_back(Pair("error", ""));
            } else {
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", strError.c_str()));
            }

            resultsObj.push_back(statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if (strCommand == "alias") {
        std::string strAlias = params[3].get_str();
        std::vector<CKarmanodeConfig::CKarmanodeEntry> mnEntries;
        mnEntries = karmanodeConfig.getEntries();

        for(CKarmanodeConfig::CKarmanodeEntry mne : karmanodeConfig.getEntries()) {

            if( strAlias != mne.getAlias()) continue;

            std::string errorMessage;
            std::vector<unsigned char> vchMasterNodeSignature;
            std::string strMasterNodeSignMessage;

            CPubKey pubKeyCollateralAddress;
            CKey keyCollateralAddress;
            CPubKey pubKeyKarmanode;
            CKey keyKarmanode;

            UniValue statusObj(UniValue::VOBJ);

            if(!obfuScationSigner.SetKey(mne.getPrivKey(), errorMessage, keyKarmanode, pubKeyKarmanode)){
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Karmanode signing error, could not set key correctly: " + errorMessage));
                resultsObj.push_back(statusObj);
                continue;
            }

            CKarmanode* pmn = mnodeman.Find(pubKeyKarmanode);
            if(pmn == NULL)
            {
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Can't find karmanode by pubkey"));
                resultsObj.push_back(statusObj);
                continue;
            }

            CBudgetVote vote(pmn->vin, hash, nVote);
            if(!vote.Sign(keyKarmanode, pubKeyKarmanode)){
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", "Failure to sign."));
                resultsObj.push_back(statusObj);
                continue;
            }

            std::string strError = "";
            if(budget.UpdateProposal(vote, NULL, strError)) {
                budget.mapSeenKarmanodeBudgetVotes.insert(make_pair(vote.GetHash(), vote));
                vote.Relay();
                success++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "success"));
                statusObj.push_back(Pair("error", ""));
            } else {
                failed++;
                statusObj.push_back(Pair("node", mne.getAlias()));
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("error", strError.c_str()));
            }

            resultsObj.push_back(statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    return NullUniValue;
}

UniValue getbudgetvotes(const UniValue& params, bool fHelp)
{
    if (params.size() != 1)
        throw runtime_error(
            "getbudgetvotes \"proposal-name\"\n"
            "\nPrint vote information for a budget proposal\n"

            "\nArguments:\n"
            "1. \"proposal-name\":      (string, required) Name of the proposal\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"mnId\": \"xxxx\",        (string) Hash of the karmanode's collateral transaction\n"
            "    \"nHash\": \"xxxx\",       (string) Hash of the vote\n"
            "    \"Vote\": \"YES|NO\",      (string) Vote cast ('YES' or 'NO')\n"
            "    \"nTime\": xxxx,         (numeric) Time in seconds since epoch the vote was cast\n"
            "    \"fValid\": true|false,  (boolean) 'true' if the vote is valid, 'false' otherwise\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getbudgetvotes", "\"test-proposal\"") + HelpExampleRpc("getbudgetvotes", "\"test-proposal\""));

    std::string strProposalName = SanitizeString(params[0].get_str());

    UniValue ret(UniValue::VARR);

    CBudgetProposal* pbudgetProposal = budget.FindProposal(strProposalName);

    if (pbudgetProposal == NULL) throw runtime_error("Unknown proposal name");

    std::map<uint256, CBudgetVote>::iterator it = pbudgetProposal->mapVotes.begin();
    while (it != pbudgetProposal->mapVotes.end()) {
        UniValue bObj(UniValue::VOBJ);
        bObj.push_back(Pair("mnId", (*it).second.vin.prevout.hash.ToString()));
        bObj.push_back(Pair("nHash", (*it).first.ToString().c_str()));
        bObj.push_back(Pair("Vote", (*it).second.GetVoteString()));
        bObj.push_back(Pair("nTime", (int64_t)(*it).second.nTime));
        bObj.push_back(Pair("fValid", (*it).second.fValid));

        ret.push_back(bObj);

        it++;
    }

    return ret;
}

UniValue getnextsuperblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getnextsuperblock\n"
            "\nPrint the next super block height\n"

            "\nResult:\n"
            "n      (numeric) Block height of the next super block\n"
            "\nExamples:\n" +
            HelpExampleCli("getnextsuperblock", "") + HelpExampleRpc("getnextsuperblock", ""));

    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return "unknown";

    int nNext = pindexPrev->nHeight - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks() + GetBudgetPaymentCycleBlocks();
    return nNext;
}

UniValue getbudgetprojection(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getbudgetprojection\n"
            "\nShow the projection of which proposals will be paid the next cycle\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"Name\": \"xxxx\",               (string) Proposal name\n"
            "    \"URL\": \"xxxx\",                (string) Proposal URL\n"
            "    \"Hash\": \"xxxx\",               (string) Proposal vote hash\n"
            "    \"FeeHash\": \"xxxx\",            (string) Proposal fee hash\n"
            "    \"BlockStart\": n,              (numeric) Proposal starting block\n"
            "    \"BlockEnd\": n,                (numeric) Proposal ending block\n"
            "    \"TotalPaymentCount\": n,       (numeric) Number of payments\n"
            "    \"RemainingPaymentCount\": n,   (numeric) Number of remaining payments\n"
            "    \"PaymentAddress\": \"xxxx\",     (string) Ohmcoin address of payment\n"
            "    \"Ratio\": x.xxx,               (numeric) Ratio of yeas vs nays\n"
            "    \"Yeas\": n,                    (numeric) Number of yea votes\n"
            "    \"Nays\": n,                    (numeric) Number of nay votes\n"
            "    \"Abstains\": n,                (numeric) Number of abstains\n"
            "    \"TotalPayment\": xxx.xxx,      (numeric) Total payment amount\n"
            "    \"MonthlyPayment\": xxx.xxx,    (numeric) Monthly payment amount\n"
            "    \"IsEstablished\": true|false,  (boolean) Established (true) or (false)\n"
            "    \"IsValid\": true|false,        (boolean) Valid (true) or Invalid (false)\n"
            "    \"IsValidReason\": \"xxxx\",      (string) Error message, if any\n"
            "    \"fValid\": true|false,         (boolean) Valid (true) or Invalid (false)\n"
            "    \"Alloted\": xxx.xxx,           (numeric) Amount alloted in current period\n"
            "    \"TotalBudgetAlloted\": xxx.xxx (numeric) Total alloted\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getbudgetprojection", "") + HelpExampleRpc("getbudgetprojection", ""));

    UniValue ret(UniValue::VARR);
    UniValue resultObj(UniValue::VOBJ);
    CAmount nTotalAllotted = 0;

    std::vector<CBudgetProposal*> winningProps = budget.GetBudget();
    for (CBudgetProposal* pbudgetProposal : winningProps) {
        nTotalAllotted += pbudgetProposal->GetAllotted();

        UniValue bObj(UniValue::VOBJ);
        budgetToJSON(pbudgetProposal, bObj);
        bObj.push_back(Pair("Alloted", ValueFromAmount(pbudgetProposal->GetAllotted())));
        bObj.push_back(Pair("TotalBudgetAlloted", ValueFromAmount(nTotalAllotted)));

        ret.push_back(bObj);
    }

    return ret;
}

UniValue getbudgetinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getbudgetinfo ( \"proposal\" )\n"
            "\nShow current karmanode budgets\n"

            "\nArguments:\n"
            "1. \"proposal\"    (string, optional) Proposal name\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"Name\": \"xxxx\",               (string) Proposal name\n"
            "    \"URL\": \"xxxx\",                (string) Proposal URL\n"
            "    \"Hash\": \"xxxx\",               (string) Proposal vote hash\n"
            "    \"FeeHash\": \"xxxx\",            (string) Proposal fee hash\n"
            "    \"BlockStart\": n,              (numeric) Proposal starting block\n"
            "    \"BlockEnd\": n,                (numeric) Proposal ending block\n"
            "    \"TotalPaymentCount\": n,       (numeric) Number of payments\n"
            "    \"RemainingPaymentCount\": n,   (numeric) Number of remaining payments\n"
            "    \"PaymentAddress\": \"xxxx\",     (string) Ohmcoin address of payment\n"
            "    \"Ratio\": x.xxx,               (numeric) Ratio of yeas vs nays\n"
            "    \"Yeas\": n,                    (numeric) Number of yea votes\n"
            "    \"Nays\": n,                    (numeric) Number of nay votes\n"
            "    \"Abstains\": n,                (numeric) Number of abstains\n"
            "    \"TotalPayment\": xxx.xxx,      (numeric) Total payment amount\n"
            "    \"MonthlyPayment\": xxx.xxx,    (numeric) Monthly payment amount\n"
            "    \"IsEstablished\": true|false,  (boolean) Established (true) or (false)\n"
            "    \"IsValid\": true|false,        (boolean) Valid (true) or Invalid (false)\n"
            "    \"IsValidReason\": \"xxxx\",      (string) Error message, if any\n"
            "    \"fValid\": true|false,         (boolean) Valid (true) or Invalid (false)\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getbudgetprojection", "") + HelpExampleRpc("getbudgetprojection", ""));

    UniValue ret(UniValue::VARR);

    std::string strShow = "valid";
    if (params.size() == 1) {
        std::string strProposalName = SanitizeString(params[0].get_str());
        CBudgetProposal* pbudgetProposal = budget.FindProposal(strProposalName);
        if (pbudgetProposal == NULL) throw runtime_error("Unknown proposal name");
        UniValue bObj(UniValue::VOBJ);
        budgetToJSON(pbudgetProposal, bObj);
        ret.push_back(bObj);
        return ret;
    }

    std::vector<CBudgetProposal*> winningProps = budget.GetAllProposals();
    for (CBudgetProposal* pbudgetProposal : winningProps) {
        if (strShow == "valid" && !pbudgetProposal->fValid) continue;

        UniValue bObj(UniValue::VOBJ);
        budgetToJSON(pbudgetProposal, bObj);

        ret.push_back(bObj);
    }

    return ret;
}

UniValue knbudgetrawvote(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 6)
        throw runtime_error(
            "knbudgetrawvote \"karmanode-tx-hash\" karmanode-tx-index \"proposal-hash\" yes|no time \"vote-sig\"\n"
            "\nCompile and relay a proposal vote with provided external signature instead of signing vote internally\n"

            "\nArguments:\n"
            "1. \"karmanode-tx-hash\"  (string, required) Transaction hash for the karmanode\n"
            "2. karmanode-tx-index   (numeric, required) Output index for the karmanode\n"
            "3. \"proposal-hash\"       (string, required) Proposal vote hash\n"
            "4. yes|no                (boolean, required) Vote to cast\n"
            "5. time                  (numeric, required) Time since epoch in seconds\n"
            "6. \"vote-sig\"            (string, required) External signature\n"

            "\nResult:\n"
            "\"status\"     (string) Vote status or error message\n"
            "\nExamples:\n" +
            HelpExampleCli("knbudgetrawvote", "") + HelpExampleRpc("knbudgetrawvote", ""));

    uint256 hashMnTx = ParseHashV(params[0], "mn tx hash");
    int nMnTxIndex = params[1].get_int();
    CTxIn vin = CTxIn(hashMnTx, nMnTxIndex);

    uint256 hashProposal = ParseHashV(params[2], "Proposal hash");
    std::string strVote = params[3].get_str();

    if (strVote != "yes" && strVote != "no") return "You can only vote 'yes' or 'no'";
    int nVote = VOTE_ABSTAIN;
    if (strVote == "yes") nVote = VOTE_YES;
    if (strVote == "no") nVote = VOTE_NO;

    int64_t nTime = params[4].get_int64();
    std::string strSig = params[5].get_str();
    bool fInvalid = false;
    vector<unsigned char> vchSig = DecodeBase64(strSig.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CKarmanode* pmn = mnodeman.Find(vin);
    if (pmn == NULL) {
        return "Failure to find karmanode in list : " + vin.ToString();
    }

    CBudgetVote vote(vin, hashProposal, nVote);
    vote.nTime = nTime;
    vote.vchSig = vchSig;

    if (!vote.SignatureValid(true)) {
        return "Failure to verify signature.";
    }

    std::string strError = "";
    if (budget.UpdateProposal(vote, NULL, strError)) {
        budget.mapSeenKarmanodeBudgetVotes.insert(make_pair(vote.GetHash(), vote));
        vote.Relay();
        return "Voted successfully";
    } else {
        return "Error voting : " + strError;
    }
}

UniValue knfinalbudget(const UniValue& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp ||
        (strCommand != "suggest" && strCommand != "vote-many" && strCommand != "vote" && strCommand != "show" && strCommand != "getvotes"))
        throw runtime_error(
            "knfinalbudget \"command\"... ( \"passphrase\" )\n"
            "Vote or show current budgets\n"
            "\nAvailable commands:\n"
            "  vote-many   - Vote on a finalized budget\n"
            "  vote        - Vote on a finalized budget\n"
            "  show        - Show existing finalized budgets\n"
            "  getvotes     - Get vote information for each finalized budget\n");

    if (strCommand == "vote-many") {
        if (params.size() != 2)
            throw runtime_error("Correct usage is 'knfinalbudget vote-many BUDGET_HASH'");

        std::string strHash = params[1].get_str();
        uint256 hash(strHash);

        int success = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VOBJ);

        for (CKarmanodeConfig::CKarmanodeEntry mne : karmanodeConfig.getEntries()) {
            std::string errorMessage;
            std::vector<unsigned char> vchMasterNodeSignature;
            std::string strMasterNodeSignMessage;

            CPubKey pubKeyCollateralAddress;
            CKey keyCollateralAddress;
            CPubKey pubKeyKarmanode;
            CKey keyKarmanode;

            UniValue statusObj(UniValue::VOBJ);

            if (!obfuScationSigner.SetKey(mne.getPrivKey(), errorMessage, keyKarmanode, pubKeyKarmanode)) {
                failed++;
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("errorMessage", "Karmanode signing error, could not set key correctly: " + errorMessage));
                resultsObj.push_back(Pair(mne.getAlias(), statusObj));
                continue;
            }

            CKarmanode* pmn = mnodeman.Find(pubKeyKarmanode);
            if (pmn == NULL) {
                failed++;
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("errorMessage", "Can't find karmanode by pubkey"));
                resultsObj.push_back(Pair(mne.getAlias(), statusObj));
                continue;
            }


            CFinalizedBudgetVote vote(pmn->vin, hash);
            if (!vote.Sign(keyKarmanode, pubKeyKarmanode)) {
                failed++;
                statusObj.push_back(Pair("result", "failed"));
                statusObj.push_back(Pair("errorMessage", "Failure to sign."));
                resultsObj.push_back(Pair(mne.getAlias(), statusObj));
                continue;
            }

            std::string strError = "";
            if (budget.UpdateFinalizedBudget(vote, NULL, strError)) {
                budget.mapSeenFinalizedBudgetVotes.insert(make_pair(vote.GetHash(), vote));
                vote.Relay();
                success++;
                statusObj.push_back(Pair("result", "success"));
            } else {
                failed++;
                statusObj.push_back(Pair("result", strError.c_str()));
            }

            resultsObj.push_back(Pair(mne.getAlias(), statusObj));
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Voted successfully %d time(s) and failed %d time(s).", success, failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if (strCommand == "vote") {
        if (params.size() != 2)
            throw runtime_error("Correct usage is 'knfinalbudget vote BUDGET_HASH'");

        std::string strHash = params[1].get_str();
        uint256 hash(strHash);

        CPubKey pubKeyKarmanode;
        CKey keyKarmanode;
        std::string errorMessage;

        if (!obfuScationSigner.SetKey(strMasterNodePrivKey, errorMessage, keyKarmanode, pubKeyKarmanode))
            return "Error upon calling SetKey";

        CKarmanode* pmn = mnodeman.Find(activeKarmanode.vin);
        if (pmn == NULL) {
            return "Failure to find karmanode in list : " + activeKarmanode.vin.ToString();
        }

        CFinalizedBudgetVote vote(activeKarmanode.vin, hash);
        if (!vote.Sign(keyKarmanode, pubKeyKarmanode)) {
            return "Failure to sign.";
        }

        std::string strError = "";
        if (budget.UpdateFinalizedBudget(vote, NULL, strError)) {
            budget.mapSeenFinalizedBudgetVotes.insert(make_pair(vote.GetHash(), vote));
            vote.Relay();
            return "success";
        } else {
            return "Error voting : " + strError;
        }
    }

    if (strCommand == "show") {
        UniValue resultObj(UniValue::VOBJ);

        std::vector<CFinalizedBudget*> winningFbs = budget.GetFinalizedBudgets();
        for(CFinalizedBudget* finalizedBudget : winningFbs)
        {
            // Ignore old finalized budgets to avoid displaying misleading error
            // messages about missing proposals.  Include the previous final budget cycle.
            if (finalizedBudget->GetBlockStart() < (chainActive.Tip()->nHeight - GetBudgetPaymentCycleBlocks())) continue;

            UniValue bObj(UniValue::VOBJ);
            bObj.push_back(Pair("FeeTX", finalizedBudget->nFeeTXHash.ToString()));
            bObj.push_back(Pair("Hash", finalizedBudget->GetHash().ToString()));
            bObj.push_back(Pair("BlockStart", (int64_t)finalizedBudget->GetBlockStart()));
            bObj.push_back(Pair("BlockEnd", (int64_t)finalizedBudget->GetBlockEnd()));
            bObj.push_back(Pair("Proposals", finalizedBudget->GetProposals()));
            bObj.push_back(Pair("VoteCount", (int64_t)finalizedBudget->GetVoteCount()));
            bObj.push_back(Pair("Status", finalizedBudget->GetStatus()));

            std::string strError = "";
            bObj.push_back(Pair("IsValid", finalizedBudget->IsValid(strError)));
            bObj.push_back(Pair("IsValidReason", strError.c_str()));

            resultObj.push_back(Pair(finalizedBudget->GetName(), bObj));
        }

        return resultObj;
    }

    if (strCommand == "getvotes") {
        if (params.size() != 2)
            throw runtime_error("Correct usage is 'knbudget getvotes budget-hash'");

        std::string strHash = params[1].get_str();
        uint256 hash(strHash);

        UniValue obj(UniValue::VOBJ);

        CFinalizedBudget* pfinalBudget = budget.FindFinalizedBudget(hash);

        if (pfinalBudget == NULL) return "Unknown budget hash";

        std::map<uint256, CFinalizedBudgetVote>::iterator it = pfinalBudget->mapVotes.begin();
        while (it != pfinalBudget->mapVotes.end()) {
            UniValue bObj(UniValue::VOBJ);
            bObj.push_back(Pair("nHash", (*it).first.ToString().c_str()));
            bObj.push_back(Pair("nTime", (int64_t)(*it).second.nTime));
            bObj.push_back(Pair("fValid", (*it).second.fValid));

            obj.push_back(Pair((*it).second.vin.prevout.ToStringShort(), bObj));

            it++;
        }

        return obj;
    }

    return NullUniValue;
}

UniValue checkbudgets(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "checkbudgets\n"
            "\nInitiates a buddget check cycle manually\n"
            "\nExamples:\n" +
            HelpExampleCli("checkbudgets", "") + HelpExampleRpc("checkbudgets", ""));

    budget.CheckAndRemove();

    return NullUniValue;
}
