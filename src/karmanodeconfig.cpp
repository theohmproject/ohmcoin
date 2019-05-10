// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "net.h"
#include "karmanodeconfig.h"
#include "util.h"
#include "ui_interface.h"
#include "base58.h"
// clang-format on

CKarmanodeConfig karmanodeConfig;

void CKarmanodeConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex)
{
    CKarmanodeEntry cme(alias, ip, privKey, txHash, outputIndex);
    entries.push_back(cme);
}

bool CKarmanodeConfig::read(std::string& strErr)
{
    int linenumber = 1;
    boost::filesystem::path pathKarmanodeConfigFile = GetKarmanodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathKarmanodeConfigFile);

    if (!streamConfig.good()) {
        FILE* configFile = fopen(pathKarmanodeConfigFile.string().c_str(), "a");
        if (configFile != NULL) {
            std::string strHeader = "# Karmanode config file\n"
                                    "# Format: alias IP:port karmanodeprivkey collateral_output_txid collateral_output_index\n"
                                    "# Example: mn1 127.0.0.2:52020 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
            fclose(configFile);
        }
        return true; // Nothing to read, so just return
    }

    for (std::string line; std::getline(streamConfig, line); linenumber++) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex;

        if (iss >> comment) {
            if (comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                strErr = _("Could not parse karmanode.conf") + "\n" +
                         strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
                streamConfig.close();
                return false;
            }
        }

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (CService(ip).GetPort() != 52020) {
                strErr = _("Invalid port detected in karmanode.conf") + "\n" +
                         strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                         _("(must be 52020 for mainnet)");
                streamConfig.close();
                return false;
            }
        } else if (CService(ip).GetPort() == 52020) {
            strErr = _("Invalid port detected in karmanode.conf") + "\n" +
                     strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                     _("(52020 could be used only on mainnet)");
            streamConfig.close();
            return false;
        }


        add(alias, ip, privKey, txHash, outputIndex);
    }

    streamConfig.close();
    return true;
}

bool CKarmanodeConfig::CKarmanodeEntry::castOutputIndex(int &n)
{
    try {
        n = std::stoi(outputIndex);
    } catch (const std::exception e) {
        LogPrintf("%s: %s on getOutputIndex\n", __func__, e.what());
        return false;
    }

    return true;
}

void CKarmanodeConfig::clear()
{
	entries.clear();
}

void CKarmanodeConfig::deleteAlias(int count)
{
	count = count - 1;
	entries.erase(entries.begin()+count);
}

void CKarmanodeConfig::writeToKarmanodeConf()
{
    boost::filesystem::path pathKarmanodeConfigFile = GetKarmanodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathKarmanodeConfigFile);

    FILE* configFile = fopen(pathKarmanodeConfigFile.string().c_str(), "w");

	// Add file header back as each time this runs it restarts the file
    std::string strHeader = "# Karmanode config file\n"
                            "# Format: alias IP:port karmanodeprivkey collateral_output_txid collateral_output_index\n"
                            "# Example: mn1 127.0.0.2:52020 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
    fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
	
	std::string karmanodeAliasBase = "";
	
	BOOST_FOREACH (CKarmanodeConfig::CKarmanodeEntry mne, karmanodeConfig.getEntries()) {
		// Orders configs in proper strings
		std::string karmanodeAliasLine  = mne.getAlias() + " " + mne.getIp() + " " + mne.getPrivKey() + " " + mne.getTxHash() + " " + mne.getOutputIndex() + "\n";
		karmanodeAliasBase = karmanodeAliasBase + karmanodeAliasLine;
	}
	//Writes it to the string
	fwrite(karmanodeAliasBase.c_str(), std::strlen(karmanodeAliasBase.c_str()), 1, configFile);
	// When done adding all the karmanodes to the config close the file
    fclose(configFile);
	clear();
	std::string strErr;
	read(strErr);
}