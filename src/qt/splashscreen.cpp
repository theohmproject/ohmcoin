// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2020 The OHMCOIN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "splashscreen.h"

#include "clientversion.h"
#include "init.h"
#include "networkstyle.h"
#include "ui_interface.h"
#include "util.h"
#include "version.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QPainter>

Splash::Splash(const NetworkStyle* networkStyle)
    : QWidget(), curAlignment(0)
{
    // set reference point, paddings
    int paddingLeft = 14;
    int paddingTop = 470;
    int titleVersionVSpace = 17;
    int titleCopyrightVSpace = 32;

    float fontFactor = 1.0;

    // define text to place
    QString titleText = tr("Ohmcoin Core");
    QString versionText = QString(tr("Version %1")).arg(QString::fromStdString(FormatFullVersion()));
    QString copyrightTextBtc = QChar(0xA9) + QString(" 2009-%1 ").arg(COPYRIGHT_YEAR) + QString(tr("The Bitcoin Core developers"));
    QString copyrightTextDash = QChar(0xA9) + QString(" 2014-%1 ").arg(COPYRIGHT_YEAR) + QString(tr("The Dash Core developers"));
    QString copyrightTextPIVX = QChar(0xA9) + QString(" 2015-%1 ").arg(COPYRIGHT_YEAR) + QString(tr("The PIVX Core developers"));
    QString copyrightTextOhmcoin = QChar(0xA9) + QString(" 2017-%1 ").arg(COPYRIGHT_YEAR) + QString(tr("The Ohmcoin Core developers"));
    QString titleAddText = networkStyle->getTitleAddText();

    QString font = QApplication::font().toString();

    // load the bitmap for writing some text over it

    pixmap = networkStyle->getSplashImage();
   // pixmap = pixmap.scaled(QSize(400,400), Qt::KeepAspectRatio,Qt::SmoothTransformation);

    QPainter pixPaint(&pixmap);
    pixPaint.setRenderHint(QPainter::Antialiasing);
    pixPaint.setRenderHint(QPainter::HighQualityAntialiasing);
    pixPaint.setPen(QColor(73, 84, 94));

    // check font size and drawing with
    pixPaint.setFont(QFont(font, 28 * fontFactor));
    QFontMetrics fm = pixPaint.fontMetrics();
    int titleTextWidth = fm.width(titleText);
    if (titleTextWidth > 160) {
        // strange font rendering, Arial probably not found
        fontFactor = 0.75;
    }

    pixPaint.setFont(QFont(font, 28 * fontFactor));
    fm = pixPaint.fontMetrics();
    //titleTextWidth = fm.width(titleText);
    pixPaint.drawText(paddingLeft, paddingTop, titleText);

    pixPaint.setFont(QFont(font, 15 * fontFactor));
    pixPaint.drawText(paddingLeft, paddingTop + titleVersionVSpace, versionText);

    // draw copyright stuff
    pixPaint.setFont(QFont(font, 10 * fontFactor));
    pixPaint.drawText(paddingLeft, paddingTop + titleCopyrightVSpace, copyrightTextBtc);
    pixPaint.drawText(paddingLeft, paddingTop + titleCopyrightVSpace + 12, copyrightTextDash);
    pixPaint.drawText(paddingLeft, paddingTop + titleCopyrightVSpace + 24, copyrightTextPIVX);
    pixPaint.drawText(paddingLeft, paddingTop + titleCopyrightVSpace + 36, copyrightTextOhmcoin);

    // draw additional text if special network
    // if (!titleAddText.isEmpty()) {
    //     QFont boldFont = QFont(font, 10 * fontFactor);
    //     boldFont.setWeight(QFont::Bold);
    //     pixPaint.setFont(boldFont);
    //     fm = pixPaint.fontMetrics();
    //     int titleAddTextWidth = fm.width(titleAddText);
    //     pixPaint.drawText(pixmap.width() - titleAddTextWidth - 10, pixmap.height() - 25, titleAddText);
    // }

    pixPaint.end();

    // Set window title
    setWindowTitle(titleText + " " + titleAddText);

    // Resize window and move to center of desktop, disallow resizing
    //QRect r(QPoint(), pixmap.size());
    QRect r(QPoint(), QSize(pixmap.size().width()/2, pixmap.size().height()/2)); //retina
    resize(r.size());
    setFixedSize(r.size());
    move(QApplication::desktop()->screenGeometry().center() - r.center());

    subscribeToCoreSignals();
}

Splash::~Splash()
{
    unsubscribeFromCoreSignals();
}

void Splash::slotFinish(QWidget* mainWin)
{
    Q_UNUSED(mainWin);
    hide();
}

static void InitMessage(Splash* splash, const std::string& message)
{
    QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom | Qt::AlignHCenter),
        Q_ARG(QColor, QColor(73, 84, 94)));
}

static void ShowProgress(Splash* splash, const std::string& title, int nProgress)
{
    InitMessage(splash, title + strprintf("%d", nProgress) + "%");
}

#ifdef ENABLE_WALLET
static void ConnectWallet(Splash* splash, CWallet* wallet)
{
    wallet->ShowProgress.connect(boost::bind(ShowProgress, splash, boost::arg<1>(), boost::arg<2>()));
}
#endif

void Splash::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.InitMessage.connect(boost::bind(InitMessage, this, boost::arg<1>()));
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this, boost::arg<1>(), boost::arg<2>()));
#ifdef ENABLE_WALLET
    uiInterface.LoadWallet.connect(boost::bind(ConnectWallet, this, boost::arg<1>()));
#endif
}

void Splash::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.InitMessage.disconnect(boost::bind(InitMessage, this, boost::arg<1>()));
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this, boost::arg<1>(), boost::arg<2>()));
#ifdef ENABLE_WALLET
    if (pwalletMain)
        pwalletMain->ShowProgress.disconnect(boost::bind(ShowProgress, this, boost::arg<1>(), boost::arg<2>()));
#endif
}

void Splash::showMessage(const QString& message, int alignment, const QColor& color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void Splash::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(5, 5, -5, -5);
    painter.setPen(curColor);
    painter.drawText(r, curAlignment, curMessage);
}

void Splash::closeEvent(QCloseEvent* event)
{
    StartShutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
