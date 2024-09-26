// Copyright (c) 2018 The Phore developers
// Copyright (c) 2018 The Curium developers
// Copyright (c) 2017-2018 The Bulwark Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_PROPOSALLIST_H
#define BITCOIN_QT_PROPOSALLIST_H

#include "columnalignedlayout.h"
#include "guiutil.h"
#include "proposaltablemodel.h"

#include <QKeyEvent>
#include <QTimer>
#include <QWidget>

class ProposalFilterProxy;
class CKarmanode;
class WalletModel;

QT_BEGIN_NAMESPACE
class QComboBox;
class QDateTimeEdit;
class QFrame;
class QItemSelectionModel;
class QLineEdit;
class QMenu;
class QModelIndex;
class QSignalMapper;
class QTableView;
QT_END_NAMESPACE

#define PROPOSALLIST_UPDATE_SECONDS 30

class ProposalList : public QWidget
{
    Q_OBJECT

public:
    explicit ProposalList(QWidget *parent = 0);

    void setModel(WalletModel* model);

    enum DateEnum
    {
        All,
        Today,
        ThisWeek,
        ThisMonth,
        LastMonth,
        ThisYear,
        Range
    };

    enum ColumnWidths {
        PROPOSAL_COLUMN_WIDTH = 300,
        AMOUNT_COLUMN_WIDTH = 110,
        START_DATE_COLUMN_WIDTH = 90,
        END_DATE_COLUMN_WIDTH = 90,
        TOTAL_PAYMENT_COLUMN_WIDTH = 80,
        REMAINING_PAYMENT_COLUMN_WIDTH = 80,
        YES_VOTES_COLUMN_WIDTH = 60,
        NO_VOTES_COLUMN_WIDTH = 60,
        ABSTAIN_COLUMN_WIDTH = 60,
        VOTES_NEEDED_COLUMN_WIDTH = 110,
        MINIMUM_COLUMN_WIDTH = 23
    };

private:
    WalletModel* model;
    ProposalFilterProxy *proposalProxyModel;
    ProposalTableModel *proposalTableModel;
    QTableView *proposalList;
    int64_t nLastUpdate = 0;

    QLineEdit *proposalWidget;
    QLineEdit *startDateWidget;
    QLineEdit *endDateWidget;
    QLineEdit *totalPaymentCountWidget;
    QLineEdit *remainingPaymentCountWidget;
    QTimer *timer;

    QLineEdit *yesVotesWidget;
    QLineEdit *noVotesWidget;
    QLineEdit *abstainVotesWidget;
    QLineEdit *amountWidget;
    QLineEdit *votesNeededWidget;
    QLabel *secondsLabel;

    QMenu *contextMenu;

    QLineEdit *proposalStartDate;
    QLineEdit *proposalEndDate;

    ColumnAlignedLayout *hlayout;

    /* Header - Info/Projection */
    QComboBox *proposalTypeCombo;
    QHBoxLayout *headLayout;
    /* End Header - Info/Projection */

    void vote_click_handler(const std::string voteString);

    GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;

    virtual void resizeEvent(QResizeEvent* event);

private Q_SLOTS:
            void createProposal();
    void proposalType(int type);
    void contextualMenu(const QPoint &);
    void voteYes();
    void voteNo();
    void voteAbstain();
    void openProposalUrl();
    void invalidateAlignedLayout();

    Q_SIGNALS:
            void doubleClicked(const QModelIndex&);

public Q_SLOTS:
            void refreshProposals(bool force = false);
    void changedProposal(const QString &proposal);
    void chooseStartDate(const QString &startDate);
    void chooseEndDate(const QString &endDate);
    void changedTotalPaymentCount(const QString &totalPaymentCount);
    void changedRemainingPaymentCount(const QString &remainingPaymentCount);
    void changedYesVotes(const QString &minYesVotes);
    void changedNoVotes(const QString &minNoVotes);
    void changedAbstainVotes(const QString &minAbstainVotes);
    void changedVotesNeeded(const QString &votesNeeded);
    void changedAmount(const QString &minAmount);

};

#endif // BITCOIN_QT_PROPOSALLIST_H