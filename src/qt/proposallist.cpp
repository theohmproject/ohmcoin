// Copyright (c) 2018 The Phore developers
// Copyright (c) 2018 The Curium developers
// Copyright (c) 2017-2018 The Bulwark Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activekarmanode.h"
#include "wallet/db.h"
#include "guiutil.h"
#include "init.h"
#include "main.h"
#include "obfuscation.h"
#include "optionsmodel.h"
#include "proposaldialog.h"
#include "proposallist.h"
#include "proposalfilterproxy.h"
#include "proposalrecord.h"
#include "proposaltablemodel.h"
#include "karmanode-budget.h"
#include "karmanode-payments.h"
#include "karmanodeconfig.h"
#include "karmanodeman.h"
#include "walletmodel.h"
#include "rpc/server.h"
#include "ui_interface.h"
#include "utilmoneystr.h"

#include "util.h"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPoint>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>

ProposalList::ProposalList(QWidget *parent) :
        QWidget(parent), proposalTableModel(0), proposalProxyModel(0),
        proposalList(0), columnResizingFixer(0)
{
    QSettings settings;

    setContentsMargins(20,0,20,0);

    hlayout = new ColumnAlignedLayout();
    hlayout->setContentsMargins(0,0,0,0);
    hlayout->setSpacing(0);

    proposalWidget = new QLineEdit(this);
    proposalWidget->setAttribute(Qt::WA_MacShowFocusRect, 0);
#if QT_VERSION >= 0x040700
    proposalWidget->setPlaceholderText(tr("Enter proposal name"));
#endif
    proposalWidget->setObjectName("proposalWidget");
    hlayout->addWidget(proposalWidget);

    amountWidget = new QLineEdit(this);
    amountWidget->setAttribute(Qt::WA_MacShowFocusRect, 0);
#if QT_VERSION >= 0x040700
    amountWidget->setPlaceholderText(tr("Min amount"));
#endif
    amountWidget->setValidator(new QDoubleValidator(0, 1e20, 0, this));
    amountWidget->setObjectName("amountWidget");
    hlayout->addWidget(amountWidget);

    startDateWidget = new QLineEdit(this);
    startDateWidget->setAttribute(Qt::WA_MacShowFocusRect, 0);
#if QT_VERSION >= 0x040700
    startDateWidget->setPlaceholderText(tr("Start Block"));
#endif
    startDateWidget->setValidator(new QIntValidator(0, INT_MAX, this));
    startDateWidget->setObjectName("startDateWidget");
    hlayout->addWidget(startDateWidget);

    endDateWidget = new QLineEdit(this);
    endDateWidget->setAttribute(Qt::WA_MacShowFocusRect, 0);
#if QT_VERSION >= 0x040700
    endDateWidget->setPlaceholderText(tr("End Block"));
#endif
    endDateWidget->setValidator(new QIntValidator(0, INT_MAX, this));
    endDateWidget->setObjectName("endDateWidget");
    hlayout->addWidget(endDateWidget);

    totalPaymentCountWidget = new QLineEdit(this);
#if QT_VERSION >= 0x040700
    totalPaymentCountWidget->setPlaceholderText(tr("Payments"));
#endif
    totalPaymentCountWidget->setValidator(new QIntValidator(0, INT_MAX, this));
    totalPaymentCountWidget->setObjectName("totalPaymentCountWidget");
    hlayout->addWidget(totalPaymentCountWidget);

    remainingPaymentCountWidget = new QLineEdit(this);
#if QT_VERSION >= 0x040700
    remainingPaymentCountWidget->setPlaceholderText(tr("Remaining"));
#endif
    remainingPaymentCountWidget->setValidator(new QIntValidator(0, INT_MAX, this));
    remainingPaymentCountWidget->setObjectName("remainingPaymentCountWidget");
    hlayout->addWidget(remainingPaymentCountWidget);

    yesVotesWidget = new QLineEdit(this);
    yesVotesWidget->setAttribute(Qt::WA_MacShowFocusRect, 0);
#if QT_VERSION >= 0x040700
    yesVotesWidget->setPlaceholderText(tr("Min yes votes"));
#endif
    yesVotesWidget->setValidator(new QIntValidator(0, INT_MAX, this));
    yesVotesWidget->setObjectName("yesVotesWidget");
    hlayout->addWidget(yesVotesWidget);

    noVotesWidget = new QLineEdit(this);
    noVotesWidget->setAttribute(Qt::WA_MacShowFocusRect, 0);
#if QT_VERSION >= 0x040700
    noVotesWidget->setPlaceholderText(tr("Min no votes"));
#endif
    noVotesWidget->setValidator(new QIntValidator(0, INT_MAX, this));
    noVotesWidget->setObjectName("noVotesWidget");
    hlayout->addWidget(noVotesWidget);

    abstainVotesWidget = new QLineEdit(this);
    abstainVotesWidget->setAttribute(Qt::WA_MacShowFocusRect, 0);
#if QT_VERSION >= 0x040700
    abstainVotesWidget->setPlaceholderText(tr("Min abstain votes"));
#endif
    abstainVotesWidget->setValidator(new QIntValidator(0, INT_MAX, this));
    abstainVotesWidget->setObjectName("abstainVotesWidget");
    hlayout->addWidget(abstainVotesWidget);

    votesNeededWidget = new QLineEdit(this);
    votesNeededWidget->setAttribute(Qt::WA_MacShowFocusRect, 0);
#if QT_VERSION >= 0x040700
    votesNeededWidget->setPlaceholderText(tr("Min votes needed"));
#endif
    votesNeededWidget->setValidator(new QIntValidator(-100, 100, this));
    votesNeededWidget->setObjectName("votesNeededWidget");
    hlayout->addWidget(votesNeededWidget);

    /* Header - Info/Projection */
    QPushButton *createButton = new QPushButton(this);
    createButton->setIcon(QIcon(":/icons/add"));
    createButton->setText("Create");
    createButton->setToolTip(tr("Create budget proposal."));

    proposalTypeCombo = new QComboBox(this);
    proposalTypeCombo->setFixedWidth(240);
    proposalTypeCombo->addItem("All Proposals", 0);
    proposalTypeCombo->addItem("Budget Projection", 1);

    headLayout = new QHBoxLayout();

    QLabel* labelOverviewHeaderLeft = new QLabel();
    labelOverviewHeaderLeft->setObjectName(QStringLiteral("labelOverviewHeaderLeft"));
    labelOverviewHeaderLeft->setMinimumSize(QSize(464, 60));
    labelOverviewHeaderLeft->setMaximumSize(QSize(16777215, 60));
    labelOverviewHeaderLeft->setText(tr("Proposals"));
    labelOverviewHeaderLeft->setAlignment(Qt::AlignCenter);
    QFont fontHeaderLeft;
    fontHeaderLeft.setPointSize(20);
    fontHeaderLeft.setBold(true);
    fontHeaderLeft.setWeight(75);
    labelOverviewHeaderLeft->setFont(fontHeaderLeft);

    headLayout->setContentsMargins(0, 0, 0, 10);
    headLayout->addWidget(createButton);
    headLayout->addStretch(1);
    headLayout->addWidget(labelOverviewHeaderLeft);
    headLayout->addStretch(1);
    headLayout->addWidget(proposalTypeCombo);
    /* End Header - Info/Projection */

    QVBoxLayout *vlayout = new QVBoxLayout(this);
    vlayout->setSpacing(0);

    QHBoxLayout* horizontalLayout_Header = new QHBoxLayout();
    horizontalLayout_Header->setObjectName(QStringLiteral("horizontalLayout_Header"));

    QTableView *view = new QTableView(this);
    view->setAlternatingRowColors(true);
    view->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectRows);
    view->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);

    vlayout->addLayout(headLayout);
    vlayout->addLayout(hlayout);
    QSpacerItem* verticalSpacer_5 = new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Preferred);
    vlayout->addItem(verticalSpacer_5);
    vlayout->addWidget(view);

    int width = view->verticalScrollBar()->sizeHint().width();
    hlayout->addSpacing(width);
    hlayout->setTableColumnsToTrack(view->horizontalHeader());

    connect(view->horizontalHeader(), SIGNAL(sectionResized(int,int,int)), SLOT(invalidateAlignedLayout()));
    connect(view->horizontalScrollBar(), SIGNAL(valueChanged(int)), SLOT(invalidateAlignedLayout()));

    // view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    view->setTabKeyNavigation(false);
    view->setContextMenuPolicy(Qt::CustomContextMenu);

    proposalList = view;

    QHBoxLayout *actionBar = new QHBoxLayout();
    actionBar->setSpacing(11);
    actionBar->setContentsMargins(0,20,0,4);

    QPushButton *voteYesButton = new QPushButton(tr("Vote Yes"), this);
    voteYesButton->setToolTip(tr("Vote Yes on the selected proposal"));
    actionBar->addWidget(voteYesButton);

    QPushButton *voteAbstainButton = new QPushButton(tr("Vote Abstain"), this);
    voteAbstainButton->setToolTip(tr("Vote Abstain on the selected proposal"));
    actionBar->addWidget(voteAbstainButton);

    QPushButton *voteNoButton = new QPushButton(tr("Vote No"), this);
    voteNoButton->setToolTip(tr("Vote No on the selected proposal"));
    actionBar->addWidget(voteNoButton);

    secondsLabel = new QLabel();
    actionBar->addWidget(secondsLabel);
    actionBar->addStretch();

    vlayout->addLayout(actionBar);

    QAction *voteYesAction = new QAction(tr("Vote Yes"), this);
    QAction *voteAbstainAction = new QAction(tr("Vote Abstain"), this);
    QAction *voteNoAction = new QAction(tr("Vote No"), this);
    QAction *openUrlAction = new QAction(tr("Visit proposal website"), this);

    contextMenu = new QMenu(this);
    contextMenu->addAction(voteYesAction);
    contextMenu->addAction(voteAbstainAction);
    contextMenu->addAction(voteNoAction);
    contextMenu->addSeparator();
    contextMenu->addAction(openUrlAction);

    connect(view, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

    connect(createButton, SIGNAL(clicked()), this, SLOT(createProposal()));
    connect(proposalTypeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(proposalType(int)));

    connect(voteYesButton, SIGNAL(clicked()), this, SLOT(voteYes()));
    connect(voteAbstainButton, SIGNAL(clicked()), this, SLOT(voteAbstain()));
    connect(voteNoButton, SIGNAL(clicked()), this, SLOT(voteNo()));

    connect(voteYesAction, SIGNAL(triggered()), this, SLOT(voteYes()));
    connect(voteNoAction, SIGNAL(triggered()), this, SLOT(voteNo()));
    connect(voteAbstainAction, SIGNAL(triggered()), this, SLOT(voteAbstain()));
    connect(proposalWidget, SIGNAL(textChanged(QString)), this, SLOT(changedProposal(QString)));
    connect(startDateWidget, SIGNAL(textChanged(QString)), this, SLOT(chooseStartDate(QString)));
    connect(endDateWidget, SIGNAL(textChanged(QString)), this, SLOT(chooseEndDate(QString)));
    connect(totalPaymentCountWidget, SIGNAL(textChanged(QString)), this, SLOT(changedTotalPaymentCount(QString)));
    connect(remainingPaymentCountWidget, SIGNAL(textChanged(QString)), this, SLOT(changedRemainingPaymentCount(QString)));
    connect(yesVotesWidget, SIGNAL(textChanged(QString)), this, SLOT(changedYesVotes(QString)));
    connect(noVotesWidget, SIGNAL(textChanged(QString)), this, SLOT(changedNoVotes(QString)));
    connect(abstainVotesWidget, SIGNAL(textChanged(QString)), this, SLOT(changedAbstainVotes(QString)));
    connect(amountWidget, SIGNAL(textChanged(QString)), this, SLOT(changedAmount(QString)));
    connect(votesNeededWidget, SIGNAL(textChanged(QString)), this, SLOT(changedVotesNeeded(QString)));

    connect(view, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(openProposalUrl()));
    connect(view, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));
    connect(openUrlAction, SIGNAL(triggered()), this, SLOT(openProposalUrl()));

    nLastUpdate = GetTime();

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(refreshProposals()));
    timer->start(1000);
}

void ProposalList::setModel(WalletModel* model)
{

    QSettings settings;
    proposalTableModel = new ProposalTableModel(this);
    this->model = model;

    if (model) {
        proposalProxyModel = new ProposalFilterProxy(this);
        proposalProxyModel->setDynamicSortFilter(true);
        proposalProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
        proposalProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
        proposalProxyModel->setSortRole(Qt::EditRole);
        proposalProxyModel->setSourceModel(proposalTableModel);

        proposalList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        proposalList->setModel(proposalProxyModel);
        proposalList->setAlternatingRowColors(true);
        proposalList->setSelectionBehavior(QAbstractItemView::SelectRows);
        proposalList->setSortingEnabled(true);
        proposalList->sortByColumn(ProposalTableModel::StartDate, Qt::DescendingOrder);
        proposalList->verticalHeader()->hide();

        proposalList->setColumnWidth(ProposalTableModel::Proposal, PROPOSAL_COLUMN_WIDTH);
        proposalList->setColumnWidth(ProposalTableModel::Amount, AMOUNT_COLUMN_WIDTH);
        proposalList->setColumnWidth(ProposalTableModel::StartDate, START_DATE_COLUMN_WIDTH);
        proposalList->setColumnWidth(ProposalTableModel::EndDate, END_DATE_COLUMN_WIDTH);
        proposalList->setColumnWidth(ProposalTableModel::TotalPaymentCount, TOTAL_PAYMENT_COLUMN_WIDTH);
        proposalList->setColumnWidth(ProposalTableModel::RemainingPaymentCount, REMAINING_PAYMENT_COLUMN_WIDTH);
        proposalList->setColumnWidth(ProposalTableModel::YesVotes, YES_VOTES_COLUMN_WIDTH);
        proposalList->setColumnWidth(ProposalTableModel::NoVotes, NO_VOTES_COLUMN_WIDTH);
        proposalList->setColumnWidth(ProposalTableModel::AbstainVotes, ABSTAIN_COLUMN_WIDTH);
        proposalList->setColumnWidth(ProposalTableModel::VotesNeeded, VOTES_NEEDED_COLUMN_WIDTH);

        columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(proposalList, VOTES_NEEDED_COLUMN_WIDTH, MINIMUM_COLUMN_WIDTH);
    }
}

void ProposalList::createProposal()
{

    WalletModel::EncryptionStatus encStatus = model->getEncryptionStatus();

    if (encStatus == model->Locked || encStatus == model->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(model->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        ProposalDialog dlg(ProposalDialog::PrepareProposal, this);
        if (QDialog::Accepted == dlg.exec())
        {
            refreshProposals(true);
        }
        return;
    }

    ProposalDialog dlg(ProposalDialog::PrepareProposal, this);
    if (QDialog::Accepted == dlg.exec())
    {
        refreshProposals(true);
    }
}

void ProposalList::proposalType(int type)
{
    proposalTableModel->setProposalType(type);
    refreshProposals(true);
}

void ProposalList::invalidateAlignedLayout() {
    hlayout->invalidate();
}

void ProposalList::refreshProposals(bool force) {
    int64_t secondsRemaining = nLastUpdate - GetTime() + PROPOSALLIST_UPDATE_SECONDS;

    QString secOrMinutes = (secondsRemaining / 60 > 1) ? tr("minute(s)") : tr("second(s)");
    secondsLabel->setText(tr("List will be updated in %1 %2").arg((secondsRemaining > 60) ? QString::number(secondsRemaining / 60) : QString::number(secondsRemaining), secOrMinutes));

    if(secondsRemaining > 0 && !force) return;
    nLastUpdate = GetTime();

    proposalTableModel->refreshProposals();

    secondsLabel->setText(tr("List will be updated in 0 second(s)"));
}



void ProposalList::changedAmount(const QString &minAmount)
{
    if(!proposalProxyModel)
        return;

    proposalProxyModel->setMinAmount(minAmount.toInt());
}

void ProposalList::changedVotesNeeded(const QString &votesNeeded)
{
    if(!proposalProxyModel)
        return;

    int value = votesNeeded == "" ? 0 : votesNeeded.toInt();

    proposalProxyModel->setVotesNeeded(value);
}

void ProposalList::changedProposal(const QString &proposal)
{
    if(!proposalProxyModel)
        return;

    proposalProxyModel->setProposal(proposal);
}

void ProposalList::changedYesVotes(const QString &minYesVotes)
{
    if(!proposalProxyModel)
        return;

    proposalProxyModel->setMinYesVotes(minYesVotes.toInt());
}

void ProposalList::chooseStartDate(const QString &startDate)
{
    if(!proposalProxyModel)
        return;

    proposalProxyModel->setMinYesVotes(startDate.toInt());
}

void ProposalList::chooseEndDate(const QString &endDate)
{
    if(!proposalProxyModel)
        return;

    proposalProxyModel->setMinYesVotes(endDate.toInt());
}

void ProposalList::changedTotalPaymentCount(const QString &totalPaymentCount)
{
    if (!proposalProxyModel) return;
    proposalProxyModel->setTotalPaymentCount(totalPaymentCount.toInt());
}

void ProposalList::changedRemainingPaymentCount(const QString &remainingPaymentCount)
{
    if (!proposalProxyModel) return;
    proposalProxyModel->setRemainingPaymentCount(remainingPaymentCount.toInt());
}

void ProposalList::changedNoVotes(const QString &minNoVotes)
{
    if(!proposalProxyModel)
        return;

    proposalProxyModel->setMinNoVotes(minNoVotes.toInt());
}

void ProposalList::changedAbstainVotes(const QString &minAbstainVotes)
{
    if(!proposalProxyModel)
        return;

    proposalProxyModel->setMinAbstainVotes(minAbstainVotes.toInt());
}

void ProposalList::contextualMenu(const QPoint &point)
{
    QModelIndex index = proposalList->indexAt(point);
    QModelIndexList selection = proposalList->selectionModel()->selectedRows(0);
    if (selection.empty())
        return;

    if(index.isValid())
        contextMenu->exec(QCursor::pos());
}

void ProposalList::voteYes()
{
    vote_click_handler("yes");
}

void ProposalList::voteNo()
{
    vote_click_handler("no");
}

void ProposalList::voteAbstain()
{
    vote_click_handler("abstain");
}

void ProposalList::vote_click_handler(const std::string voteString)
{
    if(!proposalList->selectionModel())
        return;

    QModelIndexList selection = proposalList->selectionModel()->selectedRows();
    if(selection.empty())
        return;

    QString proposalName = selection.at(0).data(ProposalTableModel::ProposalRole).toString();

    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm vote"),
                                                               tr("Are you sure you want to vote <strong>%1</strong> on the proposal <strong>%2</strong>?").arg(QString::fromStdString(voteString), proposalName),
                                                               QMessageBox::Yes | QMessageBox::Cancel,
                                                               QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    uint256 hash;
    hash.SetHex(selection.at(0).data(ProposalTableModel::ProposalHashRole).toString().toStdString());

    int success = 0;
    int failed = 0;

    std::string strVote = voteString;
    int nVote = VOTE_ABSTAIN;
    if (strVote == "yes") nVote = VOTE_YES;
    if (strVote == "no") nVote = VOTE_NO;


    for (const auto& mne : karmanodeConfig.getEntries()) {
        std::string errorMessage;
        std::vector<unsigned char> vchkarmanodeSignature;
        std::string strkarmanodeSignMessage;

        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;
        CPubKey pubKeykarmanode;
        CKey keykarmanode;


        UniValue statusObj(UniValue::VOBJ);

        if (!obfuScationSigner.SetKey(mne.getPrivKey(), errorMessage, keykarmanode, pubKeykarmanode)) {
            failed++;
            continue;
        }

        CKarmanode* pmn = mnodeman.Find(pubKeykarmanode);
        if (pmn == NULL) {
            failed++;
            continue;
        }

        CBudgetVote vote(pmn->vin, hash, nVote);
        if (!vote.Sign(keykarmanode, pubKeykarmanode)) {
            failed++;
            continue;
        }

        std::string strError = "";
        if (budget.UpdateProposal(vote, NULL, strError)) {
            budget.mapSeenKarmanodeBudgetVotes.insert(make_pair(vote.GetHash(), vote));
            vote.Relay();
            success++;
        } else {
            failed++;
        }
    }

    QMessageBox::information(this, tr("Voting"),
                             tr("You voted %1 %2 time(s) successfully and failed %3 time(s) on %4").arg(QString::fromStdString(voteString), QString::number(success), QString::number(failed), proposalName));

    refreshProposals(true);
}

void ProposalList::openProposalUrl()
{
    if(!proposalList || !proposalList->selectionModel())
        return;

    QModelIndexList selection = proposalList->selectionModel()->selectedRows(0);
    if(!selection.isEmpty())
        QDesktopServices::openUrl(selection.at(0).data(ProposalTableModel::ProposalUrlRole).toString());
}

void ProposalList::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    columnResizingFixer->stretchColumnWidth(ProposalTableModel::Proposal);
}
