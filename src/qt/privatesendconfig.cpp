#include "privatesendconfig.h"
#include "ui_privatesendconfig.h"

#include "bitcoinunits.h"
#include "guiconstants.h"
#include "init.h"
#include "optionsmodel.h"
#include "walletmodel.h"

#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

PrivateSendConfig::PrivateSendConfig(QWidget* parent) : QDialog(parent),
                                                        ui(new Ui::PrivateSendConfig),
                                                        model(0)
{
    ui->setupUi(this);

    connect(ui->buttonBasic, SIGNAL(clicked()), this, SLOT(clickBasic()));
    connect(ui->buttonHigh, SIGNAL(clicked()), this, SLOT(clickHigh()));
    connect(ui->buttonMax, SIGNAL(clicked()), this, SLOT(clickMax()));
}

PrivateSendConfig::~PrivateSendConfig()
{
    delete ui;
}

void PrivateSendConfig::setModel(WalletModel* model)
{
    this->model = model;
}

void PrivateSendConfig::clickBasic()
{
    configure(true, 1000, 2);

    QString strAmount(BitcoinUnits::formatWithUnit(
        model->getOptionsModel()->getDisplayUnit(), 1000 * COIN));
    QMessageBox::information(this, tr("PrivateSend Configuration"),
        tr(
            "PrivateSend was successfully set to basic (%1 and 2 rounds). You can change this at any time by opening OHMC's configuration screen.")
            .arg(strAmount));

    close();
}

void PrivateSendConfig::clickHigh()
{
    configure(true, 1000, 8);

    QString strAmount(BitcoinUnits::formatWithUnit(
        model->getOptionsModel()->getDisplayUnit(), 1000 * COIN));
    QMessageBox::information(this, tr("PrivateSend Configuration"),
        tr(
            "PrivateSend was successfully set to high (%1 and 8 rounds). You can change this at any time by opening OHMC's configuration screen.")
            .arg(strAmount));

    close();
}

void PrivateSendConfig::clickMax()
{
    configure(true, 1000, 16);

    QString strAmount(BitcoinUnits::formatWithUnit(
        model->getOptionsModel()->getDisplayUnit(), 1000 * COIN));
    QMessageBox::information(this, tr("PrivateSend Configuration"),
        tr(
            "PrivateSend was successfully set to maximum (%1 and 16 rounds). You can change this at any time by opening OHMC's configuration screen.")
            .arg(strAmount));

    close();
}

void PrivateSendConfig::configure(bool enabled, int coins, int rounds)
{
    QSettings settings;

    settings.setValue("nPrivateSendRounds", rounds);
    settings.setValue("nAnonymizeOhmAmount", coins);

    nPrivateSendRounds = rounds;
    nAnonymizeOhmAmount = coins;
}
