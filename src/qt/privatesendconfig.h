#ifndef PRIVATESENDCONFIG_H
#define PRIVATESENDCONFIG_H

#include <QDialog>

namespace Ui
{
class PrivateSendConfig;
}
class WalletModel;

/** Multifunctional dialog to ask for passphrases. Used for encryption, unlocking, and changing the passphrase.
 */
class PrivateSendConfig : public QDialog
{
    Q_OBJECT

public:
    PrivateSendConfig(QWidget* parent = 0);
    ~PrivateSendConfig();

    void setModel(WalletModel* model);


private:
    Ui::PrivateSendConfig* ui;
    WalletModel* model;
    void configure(bool enabled, int coins, int rounds);

private slots:

    void clickBasic();
    void clickHigh();
    void clickMax();
};

#endif // PRIVATESENDCONFIG_H
