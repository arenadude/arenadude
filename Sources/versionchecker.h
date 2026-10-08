#ifndef VERSIONCHECKER_H
#define VERSIONCHECKER_H

#include "utility.h"
#include <QNetworkAccessManager>

#define VERSION QString("v1.1.0")
#define VERSION_URL AT_REPO_RAW_URL "/Version/version.json"


//Reads Version/version.json: "versionFree" lists the versions allowed to run (the last one is the latest),
//"downloadUrl" the release page ("vx.x" is replaced by the latest version) and "log" the changes of the latest.
//A new version is downloaded by the user from the release page: the app doesn't replace itself.
class VersionChecker : public QObject
{
    Q_OBJECT
public:
    VersionChecker(QObject *parent);
    ~VersionChecker();

//Variables
private:
    QNetworkAccessManager * networkManager;
    QString latestVersion;
    bool newVersion;

//Metodos
private:
    void checkUpdate(QByteArray versionJson);
    void showVersionLog(QString changesLog);

signals:
    void pDebug(QString line, DebugLevel debugLevel=Normal, QString file="VersionChecker");

public slots:
    void replyFinished(QNetworkReply *reply);
};

#endif // VERSIONCHECKER_H
