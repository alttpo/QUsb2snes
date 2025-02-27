
#ifdef Q_OS_MACOS
#include "osx/appnap.h"
#endif

#include <QDebug>

#ifndef QUSB2SNES_NOGUI
  #include <QApplication>
  #include <QMessageBox>
  #include "appui.h"
#else
  #include <QCoreApplication>
#endif

#include <QTimer>
#include <QThread>
#include <QFile>
#include <QSettings>
#include <QObject>
#include <QHostInfo>
#include <QVersionNumber>
#include <QStandardPaths>
#include <QDir>
#include <ostream>

#include "wsserver.h"
#include "devices/sd2snesfactory.h"
#include "devices/luabridge.h"
#include "devices/retroarchfactory.h"
#include "devices/snesclassicfactory.h"

std::ostream* stdLogStream = nullptr;

WSServer    wsServer;
QSettings*  globalSettings;


/*
#include "backward.hpp"
namespace backward {

backward::SignalHandling sh;

} // namespace backward*/

static QTextStream logfile;
static QTextStream debugLogFile;
//static QTextStream lowlogfile;


// QUSB2SNES_DEVEL is used for special stuff needed when working on qusb code
// For example it enable outputing all the logs entry on cout also so you see them in
// your IDE

#if !defined(GIT_TAG_VERSION)
#define QUSB2SNES_DEVEL
#endif

#ifdef QUSB2SNES_DEVEL

static QTextStream cout(stdout);

#endif

// Set this to true if you expect lot of flood message
// It's used for example on the snes classic device that check stuff on a timer
// and only the relevant information is logged but not the whole snes classic message exchange
bool    dontLogNext = false;

void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    QTextStream*    log = &logfile;
    //cout << msg;
    if (dontLogNext)
        return ;
#ifdef QT_NO_DEBUG
    QString logString = QString("%6 %5 - %7: %1").arg(localMsg.constData()).arg(context.category, 20).arg(QDateTime::currentDateTime().toString(Qt::ISODate));
#else
    QString logString = QString("%6 %5 - %7: %1 \t(%2:%3, %4)").arg(localMsg.constData()).arg(context.file).arg(context.line).arg(context.function).arg(context.category, 20).arg(QDateTime::currentDateTime().toString(Qt::ISODate));
#endif
    switch (type)
    {
        case QtDebugMsg:
            break;
        case QtCriticalMsg:
            *log << logString.arg("Critical");
            break;
        case QtWarningMsg:
            *log << logString.arg("Warning");
            break;
        case QtFatalMsg:
            *log << logString.arg("Fatal");
            *log<< "\n"; log->flush();
            #ifndef QUSB2SNES_NOGUI
            QMessageBox::critical(nullptr, QObject::tr("Critical error"), msg);
            #else
            fprintf(stderr, "Critical error :%s\n", msg.constData());
            #endif
            qApp->exit(1);
            break;
        case QtInfoMsg:
            *log << logString.arg("Info");
            break;
    }
    if (debugLogFile.device() != nullptr)
    {
        debugLogFile << logString.arg("Debug");
        debugLogFile << "\n";
        debugLogFile.flush();
    }
    if (type != QtDebugMsg)
    {
        *log << "\n";
        log->flush();
    }
#ifdef QUSB2SNES_DEVEL
    cout << QString("%1 : %2").arg(context.category, 20).arg(msg) << "\n";
    cout.flush();
#endif
}


void    startServer()
{
    quint16 port = 8080;
    QHostAddress addr(QHostAddress::Any);
    if (globalSettings->contains("listen"))
        addr = QHostInfo::fromName(globalSettings->value("listen").toString()).addresses().first();
    if (globalSettings->contains("port"))
        port = globalSettings->value("port").toUInt();
    QString status = wsServer.start(addr, port);
    if (!status.isEmpty())
    {
        fprintf(stderr, "Can't listen on localhost:8080 or the host set in the config file: %s\n", status.toLatin1().data());
        qApp->exit(1);
    }
}

int main(int ac, char *ag[])
{
#ifndef QUSB2SNES_NOGUI
    QApplication app(ac, ag);
#else
    QCoreApplication app(ac, ag);
#endif
#ifdef Q_OS_WIN
    QFile   mlog(qApp->applicationDirPath() + "/log.txt");
    QFile   mDebugLog(qApp->applicationDirPath() + "/log-debug.txt");
    QString crashFileQPath = qApp->applicationDirPath() + "/crash-log.txt";
    QByteArray bacf = crashFileQPath.toLocal8Bit();
    //char* crashFilePath = bacf.data();

#else
    const QString appDataPath = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).at(0);
    if (!QFile::exists(appDataPath))
        QDir().mkdir(appDataPath);
    QFile   mlog(appDataPath + "/log.txt");
    QFile   mDebugLog(appDataPath + "/log-debug.txt");
#endif

    //std::filebuf crashFile;
#ifndef Q_OS_WIN
    globalSettings = new QSettings("skarsnik.nyo.fr", "QUsb2Snes");
#else
    globalSettings = new QSettings("config.ini", QSettings::IniFormat);
#endif
    if (globalSettings->contains("debugLog") && globalSettings->value("debugLog").toBool())
    {
        mDebugLog.open(QIODevice::WriteOnly | QIODevice::Text);
        debugLogFile.setDevice(&mDebugLog);

        /*crashFile.open(crashFilePath, std::ios::out);
        stdLogStream = new std::ostream(&crashFile);*/
    }
    if (mlog.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        logfile.setDevice(&mlog);
        qInstallMessageHandler(myMessageOutput);
    }
    app.setApplicationName("QUsb2Snes");
    // This is only defined in the PRO file
#ifdef GIT_TAG_VERSION
    QString plop(GIT_TAG_VERSION);
    plop.remove(0, 1); // Remove the v
    app.setApplicationVersion(QVersionNumber::fromString(plop).toString());
#else
    app.setApplicationVersion("0.8");
#endif

#ifdef Q_OS_MACOS
    auto appNap = new AppNapSuspender();
    appNap->suspend();
#endif
    qInfo() << "Runing QUsb2Snes version " << qApp->applicationVersion();
    qInfo() << "Compiled against Qt" << QT_VERSION_STR << ", running" << qVersion();
    // let set some know trusted domain
    wsServer.addTrusted("http://www.multitroid.com");
    wsServer.addTrusted("http://multitroid.com");
    wsServer.addTrusted("https://multiworld.samus.link/");
    wsServer.addTrusted("http://usb2snes.com");
    wsServer.addTrusted("https://samus.link");

#ifndef QUSB2SNES_NOGUI
    AppUi*  appUi = new AppUi();
    int updatedIndex = app.arguments().indexOf("-updated");
    if (updatedIndex != -1)
        appUi->updated(app.arguments().at(updatedIndex + 1));
    appUi->sysTray->show();
#else
   if (app.arguments().contains("--version"))
   {
        fprintf(stdout, "QUsb2Snes version : %s\n", app.applicationVersion().toLocal8Bit().constData());
        return 0;
   }
   if (globalSettings->value("sd2snessupport").toBool() || app.arguments().contains("-sd2snes"))
   {
       SD2SnesFactory* sd2snesFactory = new SD2SnesFactory();
       wsServer.addDeviceFactory(sd2snesFactory);
   }
   if (globalSettings->value("retroarchdevice").toBool() || app.arguments().contains("-retroarch"))
   {
       RetroArchFactory* retroarchFactory = new RetroArchFactory();
       wsServer.addDeviceFactory(retroarchFactory);
   }
   if (globalSettings->value("luabridge").toBool() || app.arguments().contains("-luabridge"))
   {
       LuaBridge* luaBridge = new LuaBridge();
       wsServer.addDeviceFactory(luaBridge);
   }
   if (globalSettings->value("snesclassic").toBool() || app.arguments().contains("-snesclassic"))
   {
       SNESClassicFactory* snesClassic = new SNESClassicFactory();
        wsServer.addDeviceFactory(snesClassic);
   }
   QObject::connect(&wsServer, &WSServer::listenFailed, [=](const QString& err) {
   });
   QTimer::singleShot(100, &startServer);
#endif
    return app.exec();
}
