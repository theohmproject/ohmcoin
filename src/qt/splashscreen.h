// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2020 The OHMCOIN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OHMCOIN_QT_SPLASHSCREEN_H
#define OHMCOIN_QT_SPLASHSCREEN_H

#include <QWidget>

class NetworkStyle;

/** Class for the splashscreen with information of the running client.
 *
 * @note this is intentionally not a QSplashScreen. Bitcoin Core initialization
 * can take a long time, and in that case a progress window that cannot be
 * moved around and minimized has turned out to be frustrating to the user.
 */
class Splash : public QWidget
{
    Q_OBJECT

public:
    explicit Splash(const NetworkStyle *networkStyle);
    ~Splash();

protected:
    void paintEvent(QPaintEvent* event);
    void closeEvent(QCloseEvent* event);

public slots:
    /** Slot to call finish() method as it's not defined as slot */
    void slotFinish(QWidget* mainWin);

    /** Show message and progress */
    void showMessage(const QString& message, int alignment, const QColor& color);

private:
    /** Connect core signals to splash screen */
    void subscribeToCoreSignals();
    /** Disconnect core signals to splash screen */
    void unsubscribeFromCoreSignals();

    QPixmap pixmap;
    QString curMessage;
    QColor curColor;
    int curAlignment;
};

#endif // OHMCOIN_QT_SPLASHSCREEN_H

