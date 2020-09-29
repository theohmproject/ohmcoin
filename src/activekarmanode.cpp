// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activekarmanode.h"
#include "addrman.h"
#include "karmanode.h"
#include "karmanodeconfig.h"
#include "karmanodeman.h"
#include "protocol.h"
#include "spork.h"

//
// Bootup the Karmanode, look for a 3000 OHMC input and register on the network
//
void CActiveKarmanode::ManageStatus()
{
    std::string errorMessage;

    if (!fMasterNode) return;

    if (fDebug) LogPrintf("CActiveKarmanode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (Params().NetworkID() != CBaseChainParams::REGTEST && !karmanodeSync.IsBlockchainSynced()) {
        status = ACTIVE_KARMANODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveKarmanode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_KARMANODE_SYNC_IN_PROCESS) status = ACTIVE_KARMANODE_INITIAL;

    if (status == ACTIVE_KARMANODE_INITIAL) {
        CKarmanode* pmn;
        pmn = mnodeman.Find(pubKeyKarmanode);
        if (pmn != NULL) {
            pmn->Check();
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION) EnableHotColdMasterNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_KARMANODE_STARTED) {
        // Set defaults
        status = ACTIVE_KARMANODE_NOT_CAPABLE;
        notCapableReason = "";

        if (pwalletMain->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveKarmanode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (pwalletMain->GetBalance() == 0) {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveKarmanode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (strMasterNodeAddr.empty()) {
            if (!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the karmanodeaddr configuration option.";
                LogPrintf("CActiveKarmanode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strMasterNodeAddr);
        }

        // The service needs the correct default port to work properly
        if(!CKarmanodeBroadcast::CheckDefaultPort(strMasterNodeAddr, errorMessage, "CActiveKarmanode::ManageStatus()"))
            return;

        LogPrintf("CActiveKarmanode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        CNode* pnode = ConnectNode((CAddress)service, NULL, false);
        if (!pnode) {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveKarmanode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
        pnode->Release();

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if (GetMasterNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {
            if (GetInputAge(vin) < KARMANODE_MIN_CONFIRMATIONS) {
                status = ACTIVE_KARMANODE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetInputAge(vin));
                LogPrintf("CActiveKarmanode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyKarmanode;
            CKey keyKarmanode;

            if (!obfuScationSigner.SetKey(strMasterNodePrivKey, errorMessage, keyKarmanode, pubKeyKarmanode)) {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            CKarmanodeBroadcast mnb;
            if (!CreateBroadcast(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyKarmanode, pubKeyKarmanode, errorMessage, mnb)) {
                notCapableReason = "Error on Register: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            //send to all peers
            LogPrintf("CActiveKarmanode::ManageStatus() - Relay broadcast vin = %s\n", vin.ToString());
            mnb.Relay();

            LogPrintf("CActiveKarmanode::ManageStatus() - Is capable master node!\n");
            status = ACTIVE_KARMANODE_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveKarmanode::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if (!SendKarmanodePing(errorMessage)) {
        LogPrintf("CActiveKarmanode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveKarmanode::GetStatus()
{
    switch (status) {
    case ACTIVE_KARMANODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_KARMANODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Karmanode";
    case ACTIVE_KARMANODE_INPUT_TOO_NEW:
        return strprintf("Karmanode input must have at least %d confirmations", KARMANODE_MIN_CONFIRMATIONS);
    case ACTIVE_KARMANODE_NOT_CAPABLE:
        return "Not capable karmanode: " + notCapableReason;
    case ACTIVE_KARMANODE_STARTED:
        return "Karmanode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveKarmanode::SendKarmanodePing(std::string& errorMessage)
{
    if (status != ACTIVE_KARMANODE_STARTED) {
        errorMessage = "Karmanode is not in a running status";
        return false;
    }

    CPubKey pubKeyKarmanode;
    CKey keyKarmanode;

    if (!obfuScationSigner.SetKey(strMasterNodePrivKey, errorMessage, keyKarmanode, pubKeyKarmanode)) {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveKarmanode::SendKarmanodePing() - Relay Karmanode Ping vin = %s\n", vin.ToString());

    CKarmanodePing mnp(vin);
    if (!mnp.Sign(keyKarmanode, pubKeyKarmanode)) {
        errorMessage = "Couldn't sign Karmanode Ping";
        return false;
    }

    // Update lastPing for our karmanode in Karmanode list
    CKarmanode* pmn = mnodeman.Find(vin);
    if (pmn != NULL) {
        if (pmn->IsPingedWithin(KARMANODE_PING_SECONDS, mnp.sigTime)) {
            errorMessage = "Too early to send Karmanode Ping";
            return false;
        }

        pmn->lastPing = mnp;
        mnodeman.mapSeenKarmanodePing.insert(make_pair(mnp.GetHash(), mnp));

        //mnodeman.mapSeenKarmanodeBroadcast.lastPing is probably outdated, so we'll update it
        CKarmanodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenKarmanodeBroadcast.count(hash)) mnodeman.mapSeenKarmanodeBroadcast[hash].lastPing = mnp;

        mnp.Relay();

        /*
         * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
         * AFTER MIGRATION TO V12 IS DONE
         */

        if (IsSporkActive(SPORK_10_KARMANODE_PAY_UPDATED_NODES)) return true;
        // for migration purposes ping our node on old karmanodes network too
        std::string retErrorMessage;
        std::vector<unsigned char> vchMasterNodeSignature;
        int64_t masterNodeSignatureTime = GetAdjustedTime();

        std::string strMessage = service.ToString() + std::to_string(masterNodeSignatureTime) + std::to_string(false);

        if (!obfuScationSigner.SignMessage(strMessage, retErrorMessage, vchMasterNodeSignature, keyKarmanode)) {
            errorMessage = "dseep sign message failed: " + retErrorMessage;
            return false;
        }

        if (!obfuScationSigner.VerifyMessage(pubKeyKarmanode, vchMasterNodeSignature, strMessage, retErrorMessage)) {
            errorMessage = "dseep verify message failed: " + retErrorMessage;
            return false;
        }

        LogPrint("karmanode", "dseep - relaying from active mn, %s \n", vin.ToString().c_str());
        LOCK(cs_vNodes);
        for (CNode* pnode : vNodes)
            pnode->PushMessage(NetMsgType::DSEEP, vin, vchMasterNodeSignature, masterNodeSignatureTime, false);

        /*
         * END OF "REMOVE"
         */

        return true;
    } else {
        // Seems like we are trying to send a ping while the Karmanode is not registered in the network
        errorMessage = "Obfuscation Karmanode List doesn't include our Karmanode, shutting down Karmanode pinging service! " + vin.ToString();
        status = ACTIVE_KARMANODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

bool CActiveKarmanode::CreateBroadcast(std::string strService, std::string strKeyKarmanode, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CKarmanodeBroadcast &mnb, bool fOffline)
{
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyKarmanode;
    CKey keyKarmanode;

    //need correct blocks to send ping
    if (!fOffline && !karmanodeSync.IsBlockchainSynced()) {
        errorMessage = "Sync in progress. Must wait until sync is complete to start Karmanode";
        LogPrintf("CActiveKarmanode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.SetKey(strKeyKarmanode, errorMessage, keyKarmanode, pubKeyKarmanode)) {
        errorMessage = strprintf("Can't find keys for karmanode %s - %s", strService, errorMessage);
        LogPrintf("CActiveKarmanode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if (!GetMasterNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strTxHash, strOutputIndex)) {
        errorMessage = strprintf("Could not allocate vin %s:%s for karmanode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CActiveKarmanode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    CService service = CService(strService);

    // The service needs the correct default port to work properly
    if(!CKarmanodeBroadcast::CheckDefaultPort(strService, errorMessage, "CActiveKarmanode::CreateBroadcast()"))
        return false;

    addrman.Add(CAddress(service), CNetAddr("127.0.0.1"), 2 * 60 * 60);

    return CreateBroadcast(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyKarmanode, pubKeyKarmanode, errorMessage, mnb);
}

bool CActiveKarmanode::CreateBroadcast(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyKarmanode, CPubKey pubKeyKarmanode, std::string& errorMessage, CKarmanodeBroadcast &mnb)
{
	// wait for reindex and/or import to finish
	if (fImporting || fReindex) return false;

    CKarmanodePing mnp(vin);
    if (!mnp.Sign(keyKarmanode, pubKeyKarmanode)) {
        errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
        LogPrintf("CActiveKarmanode::CreateBroadcast() -  %s\n", errorMessage);
        mnb = CKarmanodeBroadcast();
        return false;
    }

    mnb = CKarmanodeBroadcast(service, vin, pubKeyCollateralAddress, pubKeyKarmanode, PROTOCOL_VERSION);
    mnb.lastPing = mnp;
    if (!mnb.Sign(keyCollateralAddress)) {
        errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
        LogPrintf("CActiveKarmanode::CreateBroadcast() - %s\n", errorMessage);
        mnb = CKarmanodeBroadcast();
        return false;
    }

    /*
     * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
     * AFTER MIGRATION TO V12 IS DONE
     */

    if (IsSporkActive(SPORK_10_KARMANODE_PAY_UPDATED_NODES)) return true;
    // for migration purposes inject our node in old karmanodes' list too
    std::string retErrorMessage;
    std::vector<unsigned char> vchMasterNodeSignature;
    int64_t masterNodeSignatureTime = GetAdjustedTime();
    std::string donationAddress = "";
    int donationPercantage = 0;

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyKarmanode.begin(), pubKeyKarmanode.end());

    std::string strMessage = service.ToString() + std::to_string(masterNodeSignatureTime) + vchPubKey + vchPubKey2 + std::to_string(PROTOCOL_VERSION) + donationAddress + std::to_string(donationPercantage);

    if (!obfuScationSigner.SignMessage(strMessage, retErrorMessage, vchMasterNodeSignature, keyCollateralAddress)) {
        errorMessage = "dsee sign message failed: " + retErrorMessage;
        LogPrintf("CActiveKarmanode::Register() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, vchMasterNodeSignature, strMessage, retErrorMessage)) {
        errorMessage = "dsee verify message failed: " + retErrorMessage;
        LogPrintf("CActiveKarmanode::Register() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    LOCK(cs_vNodes);
    for (CNode* pnode : vNodes)
        pnode->PushMessage("dsee", vin, service, vchMasterNodeSignature, masterNodeSignatureTime, pubKeyCollateralAddress, pubKeyKarmanode, -1, -1, masterNodeSignatureTime, PROTOCOL_VERSION, donationAddress, donationPercantage);

    /*
     * END OF "REMOVE"
     */

    return true;
}

bool CActiveKarmanode::GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    return GetMasterNodeVin(vin, pubkey, secretKey, "", "");
}

bool CActiveKarmanode::GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex)
{
    // Find possible candidates
    TRY_LOCK(pwalletMain->cs_wallet, fWallet);
    if (!fWallet) return false;

    vector<COutput> possibleCoins = SelectCoinsKarmanode();
    COutput* selectedOutput;

    // Find the vin
    if (!strTxHash.empty()) {
        // Let's find it
        uint256 txHash(strTxHash);
        int outputIndex;
        try {
            outputIndex = std::stoi(strOutputIndex.c_str());
        } catch (const std::exception& e) {
            LogPrintf("%s: %s on strOutputIndex\n", __func__, e.what());
            return false;
        }

        bool found = false;
        for (COutput& out : possibleCoins) {
            if (out.tx->GetHash() == txHash && out.i == outputIndex) {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if (!found) {
            LogPrintf("CActiveKarmanode::GetMasterNodeVin - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if (possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            LogPrintf("CActiveKarmanode::GetMasterNodeVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}


// Extract Karmanode vin information from output
bool CActiveKarmanode::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    CScript pubScript;

    vin = CTxIn(out.tx->GetHash(), out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);

    const CKeyID* keyID = boost::get<CKeyID>(&address1);
    if (!keyID) {
        LogPrintf("CActiveKarmanode::GetMasterNodeVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(*keyID, secretKey)) {
        LogPrintf("CActiveKarmanode::GetMasterNodeVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running Karmanode
vector<COutput> CActiveKarmanode::SelectCoinsKarmanode()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;
    vector<COutPoint> confLockedCoins;

    // Temporary unlock MN coins from karmanode.conf
    if (GetBoolArg("-mnconflock", true)) {
        uint256 mnTxHash;
        for (CKarmanodeConfig::CKarmanodeEntry mne : karmanodeConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());

            int nIndex;
            if(!mne.castOutputIndex(nIndex))
                continue;

            COutPoint outpoint = COutPoint(mnTxHash, nIndex);
            confLockedCoins.push_back(outpoint);
            pwalletMain->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Lock MN coins from karmanode.conf back if they where temporary unlocked
    if (!confLockedCoins.empty()) {
        for (COutPoint outpoint : confLockedCoins)
            pwalletMain->LockCoin(outpoint);
    }

    // Filter
    for (const COutput& out : vCoins) {
        if (out.tx->vout[out.i].nValue == MASTER_NODE_AMOUNT * COIN) { //exactly
            filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// when starting a Karmanode, this can enable to run as a hot wallet with no funds
bool CActiveKarmanode::EnableHotColdMasterNode(CTxIn& newVin, CService& newService)
{
    if (!fMasterNode) return false;

    status = ACTIVE_KARMANODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveKarmanode::EnableHotColdMasterNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
