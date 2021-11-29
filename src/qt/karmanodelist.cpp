// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2019 The Ohmcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "karmanodelist.h"
#include "ui_karmanodelist.h"

#include "configurekarmanodepage.h"

#include "activekarmanode.h"
#include "askpassphrasedialog.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "init.h"
#include "karmanode-sync.h"
#include "karmanodeconfig.h"
#include "karmanodeman.h"
#include "sync.h"
#include "wallet/wallet.h"
#include "walletmodel.h"
#include "util.h"

#include <QMessageBox>
#include <QTimer>
#include <fstream>
#include <iostream>
#include <string>

CCriticalSection cs_karmanodes;

KarmanodeList::KarmanodeList(QWidget* parent) : QWidget(parent),
                                                  ui(new Ui::KarmanodeList),
                                                  clientModel(0),
                                                  walletModel(0)
{
    ui->setupUi(this);

    ui->startButton->setEnabled(false);

    int columnAliasWidth = 100;
    int columnAddressWidth = 200;
    int columnProtocolWidth = 60;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    ui->tableWidgetMyKarmanodes->setAlternatingRowColors(true);
    ui->tableWidgetMyKarmanodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMyKarmanodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMyKarmanodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMyKarmanodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMyKarmanodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMyKarmanodes->setColumnWidth(5, columnLastSeenWidth);

    ui->tableWidgetMyKarmanodes->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->tableWidgetMyKarmanodes->horizontalHeader()->setDefaultAlignment(Qt::AlignVCenter);

    QAction* startAliasAction = new QAction(tr("Start alias"), this);
    QAction* copyAliasAction = new QAction(tr("Copy alias"), this);
    QAction* editAliasAction = new QAction(tr("Edit alias"), this);
    QAction* deleteAliasAction = new QAction(tr("Delete"), this);

    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    contextMenu->addAction(copyAliasAction);
    contextMenu->addAction(editAliasAction);
    contextMenu->addAction(deleteAliasAction);

    connect(ui->tableWidgetMyKarmanodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));
    connect(copyAliasAction, SIGNAL(triggered()), this, SLOT(copyAlias()));
    connect(editAliasAction, SIGNAL(triggered()), this, SLOT(on_editConfigureKarmanode_clicked()));
    connect(deleteAliasAction, SIGNAL(triggered()), this, SLOT(deleteAlias()));


    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    // Fill MN list
    fFilterUpdated = true;
    nTimeFilterUpdated = GetTime();
}

KarmanodeList::~KarmanodeList()
{
    delete ui;
}

void KarmanodeList::setClientModel(ClientModel* model)
{
    this->clientModel = model;
}

void KarmanodeList::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
}

void KarmanodeList::showContextMenu(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetMyKarmanodes->itemAt(point);
    if (item) contextMenu->exec(QCursor::pos());
}

void KarmanodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    for (CKarmanodeConfig::CKarmanodeEntry mne : karmanodeConfig.getEntries()) {
        if (mne.getAlias() == strAlias) {
            std::string strError;
            CKarmanodeBroadcast mnb;

            bool fSuccess = CKarmanodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            if (fSuccess) {
                strStatusHtml += "<br>Successfully started karmanode.";
                mnodeman.UpdateKarmanodeList(mnb);
                mnb.Relay();
            } else {
                strStatusHtml += "<br>Failed to start karmanode.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void KarmanodeList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    for (CKarmanodeConfig::CKarmanodeEntry mne : karmanodeConfig.getEntries()) {
        std::string strError;
        CKarmanodeBroadcast mnb;

        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CKarmanode* pmn = mnodeman.Find(txin);

        if (strCommand == "start-missing" && pmn) continue;

        bool fSuccess = CKarmanodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

        if (fSuccess) {
            nCountSuccessful++;
            mnodeman.UpdateKarmanodeList(mnb);
            mnb.Relay();
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d karmanodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void KarmanodeList::updateMyKarmanodeInfo(QString strAlias, QString strAddr, CKarmanode* pmn)
{
    LOCK(cs_mnlistupdate);

    bool fOldRowFound = false;
    int nNewRow = 0;

    for (int i = 0; i < ui->tableWidgetMyKarmanodes->rowCount(); i++) {
        if (ui->tableWidgetMyKarmanodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if (nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMyKarmanodes->rowCount();
        ui->tableWidgetMyKarmanodes->insertRow(nNewRow);
    }

    QTableWidgetItem* aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem* addrItem = new QTableWidgetItem(pmn ? QString::fromStdString(pmn->addr.ToString()) : strAddr);
    QTableWidgetItem* protocolItem = new QTableWidgetItem(QString::number(pmn ? pmn->protocolVersion : -1));
    QTableWidgetItem* statusItem = new QTableWidgetItem(QString::fromStdString(pmn ? pmn->GetStatus() : "MISSING"));
    GUIUtil::DHMSTableWidgetItem* activeSecondsItem = new GUIUtil::DHMSTableWidgetItem(pmn ? (pmn->lastPing.sigTime - pmn->sigTime) : 0);
    QTableWidgetItem* lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", pmn ? pmn->lastPing.sigTime : 0)));
    QTableWidgetItem* pubkeyItem = new QTableWidgetItem(QString::fromStdString(pmn ? EncodeDestination(CTxDestination(pmn->pubKeyCollateralAddress.GetID())) : ""));

    ui->tableWidgetMyKarmanodes->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMyKarmanodes->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMyKarmanodes->setItem(nNewRow, 2, protocolItem);
    ui->tableWidgetMyKarmanodes->setItem(nNewRow, 3, statusItem);
    ui->tableWidgetMyKarmanodes->setItem(nNewRow, 4, activeSecondsItem);
    ui->tableWidgetMyKarmanodes->setItem(nNewRow, 5, lastSeenItem);
    ui->tableWidgetMyKarmanodes->setItem(nNewRow, 6, pubkeyItem);
}

void KarmanodeList::updateMyNodeList(bool fForce)
{
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my karmanode list only once in MY_KARMANODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_KARMANODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if (nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetMyKarmanodes->setSortingEnabled(false);
    for (CKarmanodeConfig::CKarmanodeEntry mne : karmanodeConfig.getEntries()) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CKarmanode* pmn = mnodeman.Find(txin);
        updateMyKarmanodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), pmn);
    }
    ui->tableWidgetMyKarmanodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void KarmanodeList::on_startButton_clicked()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyKarmanodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyKarmanodes->item(nSelectedRow, 0)->text().toStdString();

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm karmanode start"),
        tr("Are you sure you want to start karmanode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void KarmanodeList::on_editConfigureKarmanode_clicked()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyKarmanodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyKarmanodes->item(nSelectedRow, 0)->text().toStdString();

	int count = 0;
    for (CKarmanodeConfig::CKarmanodeEntry mne : karmanodeConfig.getEntries()) {
		count = count + 1;
		if(strAlias == mne.getAlias()) {
			KarmanodeList::openEditConfigureKarmanodePage(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), QString::fromStdString(mne.getPrivKey()), QString::fromStdString(mne.getTxHash()), QString::fromStdString(mne.getOutputIndex()), count);
			break;

		}
    }
}

void KarmanodeList::on_configureKarmanodeButton_clicked()
{

    ConfigureKarmanodePage dlg(ConfigureKarmanodePage::NewConfigureKarmanode, this);
    if ( QDialog::Accepted == dlg.exec() )
    {
while (ui->tableWidgetMyKarmanodes->rowCount() > 0)
	{
		ui->tableWidgetMyKarmanodes->removeRow(0);
	}

	// clear cache
	karmanodeConfig.clear();
    // parse karmanode.conf
    std::string strErr;
    if (!karmanodeConfig.read(strErr)) {
        LogPrintf("Error reading karmanode configuration file: \n");
    }
      updateMyNodeList(true);
    }
}

void KarmanodeList::openEditConfigureKarmanodePage(QString strAlias, QString strIP, QString strPrivKey, QString strTxHash, QString strOutputIndex, int count)
{
    ConfigureKarmanodePage dlg(ConfigureKarmanodePage::EditConfigureKarmanode, this);
    dlg.loadAlias(strAlias);
	dlg.loadIP(strIP);
	dlg.loadPrivKey(strPrivKey);
	dlg.loadTxHash(strTxHash);
	dlg.loadOutputIndex(strOutputIndex);
	dlg.counter(count);
	dlg.MNAliasCache(strAlias);
    if ( QDialog::Accepted == dlg.exec() )
    {
while (ui->tableWidgetMyKarmanodes->rowCount() > 0)
	{
		ui->tableWidgetMyKarmanodes->removeRow(0);
	}

	// clear cache
	karmanodeConfig.clear();
    // parse karmanode.conf
    std::string strErr;
    if (!karmanodeConfig.read(strErr)) {
        LogPrintf("Error reading karmanode configuration file: \n");
    }
      updateMyNodeList(true);
    }
}

void KarmanodeList::deleteAlias()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyKarmanodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyKarmanodes->item(nSelectedRow, 0)->text().toStdString();
	int count = 0;
    for (CKarmanodeConfig::CKarmanodeEntry mne : karmanodeConfig.getEntries()) {
		count = count + 1;
		if(strAlias == mne.getAlias()) {
			vector<COutPoint> confLockedCoins;
			uint256 mnTxHash;
			mnTxHash.SetHex(mne.getTxHash());
            int nIndex;
            if(!mne.castOutputIndex(nIndex))
                continue;
			COutPoint outpoint = COutPoint(mnTxHash, nIndex);
			confLockedCoins.push_back(outpoint);
			pwalletMain->UnlockCoin(outpoint);
			karmanodeConfig.deleteAlias(count);
			// write to karmanode.conf
			karmanodeConfig.writeToKarmanodeConf();
while (ui->tableWidgetMyKarmanodes->rowCount() > 0)
	{
		ui->tableWidgetMyKarmanodes->removeRow(0);
	}

	// clear cache
	karmanodeConfig.clear();
    // parse karmanode.conf
    std::string strErr;
    if (!karmanodeConfig.read(strErr)) {
        LogPrintf("Error reading karmanode configuration file: \n");
    }
			updateMyNodeList(true);
			break;

		}
    }
}

void KarmanodeList::copyAlias()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyKarmanodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyKarmanodes->item(nSelectedRow, 0)->text().toStdString();

    for (CKarmanodeConfig::CKarmanodeEntry mne : karmanodeConfig.getEntries()) {

		if(strAlias == mne.getAlias()) {

			std::string fullAliasCopy = mne.getAlias() + " " + mne.getIp() + " " + mne.getPrivKey() + " " + mne.getTxHash() + " " + mne.getOutputIndex();
			GUIUtil::setClipboard(QString::fromStdString(fullAliasCopy));
			break;

		}
    }
}

void KarmanodeList::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all karmanodes start"),
        tr("Are you sure you want to start ALL karmanodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void KarmanodeList::on_startMissingButton_clicked()
{
    if (!karmanodeSync.IsKarmanodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until karmanode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing karmanodes start"),
        tr("Are you sure you want to start MISSING karmanodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void KarmanodeList::on_tableWidgetMyKarmanodes_itemSelectionChanged()
{
    if (ui->tableWidgetMyKarmanodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void KarmanodeList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}
