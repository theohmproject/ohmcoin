// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2020 The OHMCOIN developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OHMCOIN_QT_UTILITYDIALOG_H
#define OHMCOIN_QT_UTILITYDIALOG_H

#include <QDialog>
#include <QWidget>
#include <QMainWindow>


class ClientModel;

namespace Ui {
    class HelpMessageDialog;
}

/** "Help message" dialog box */
class HelpMessageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HelpMessageDialog(QWidget *parent, bool about);
    ~HelpMessageDialog();

    void printToConsole();
    void showOrPrint();

private:
    Ui::HelpMessageDialog *ui;
    QString text;

private Q_SLOTS:
    void on_okButton_accepted();
};


/** "Shutdown" window */
class ShutdownWindow : public QWidget
{
    Q_OBJECT

public:
    ShutdownWindow(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::Widget);
    static void showShutdownWindow(QMainWindow* window);

protected:
    void closeEvent(QCloseEvent *event) override;
};

#endif // OHMCOIN_QT_UTILITYDIALOG_H
