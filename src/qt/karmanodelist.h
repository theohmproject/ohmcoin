#ifndef KARMANODELIST_H
#define KARMANODELIST_H

#include "karmanode.h"
#include "platformstyle.h"
#include "sync.h"
#include "util.h"

#include <QMenu>
#include <QTimer>
#include <QWidget>

#define MY_KARMANODELIST_UPDATE_SECONDS 60
#define KARMANODELIST_UPDATE_SECONDS 15
#define KARMANODELIST_FILTER_COOLDOWN_SECONDS 3

namespace Ui
{
class KarmanodeList;
}

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Karmanode Manager page widget */
class KarmanodeList : public QWidget
{
    Q_OBJECT

public:
    explicit KarmanodeList(QWidget* parent = 0);
    ~KarmanodeList();

    void setClientModel(ClientModel* clientModel);
    void setWalletModel(WalletModel* walletModel);
    void StartAlias(std::string strAlias);
    void StartAll(std::string strCommand = "start-all");

private:
    QMenu* contextMenu;
    int64_t nTimeFilterUpdated;
    bool fFilterUpdated;

public Q_SLOTS:
    void updateMyKarmanodeInfo(QString strAlias, QString strAddr, CKarmanode* pmn);
    void updateMyNodeList(bool fForce = false);

Q_SIGNALS:

private:
    QTimer* timer;
    Ui::KarmanodeList* ui;
    ClientModel* clientModel;
    WalletModel* walletModel;
    CCriticalSection cs_mnlistupdate;
    QString strCurrentFilter;

private Q_SLOTS:
    void showContextMenu(const QPoint&);
    void on_startButton_clicked();
    void on_startAllButton_clicked();
    void on_startMissingButton_clicked();
    void on_tableWidgetMyKarmanodes_itemSelectionChanged();
    void on_UpdateButton_clicked();
};
#endif // KARMANODELIST_H
