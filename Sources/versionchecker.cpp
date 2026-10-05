#include "versionchecker.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QtWidgets>

VersionChecker::VersionChecker(QObject *parent) : QObject(parent)
{
    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(replyFinished(QNetworkReply*)));

    networkManager->get(QNetworkRequest(QUrl(VERSION_URL)));

    QSettings settings;
    QString runVersion = settings.value("runVersion", VERSION).toString();
    newVersion = (runVersion != VERSION);
    settings.setValue("runVersion", VERSION);
    qApp->setApplicationVersion(VERSION);
}


VersionChecker::~VersionChecker()
{
    delete networkManager;
}


void VersionChecker::replyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    if(reply->error() != QNetworkReply::NoError)
    {
        emit pDebug(reply->url().toString() + " --> Failed. Retrying...");
        networkManager->get(QNetworkRequest(reply->url()));
    }
    else
    {
        checkUpdate(reply->readAll());
    }
}


void VersionChecker::checkUpdate(QByteArray versionJson)
{
    //Get latest version
    QJsonObject versionJsonObject = QJsonDocument::fromJson(versionJson).object();
    const QJsonArray versionArray = versionJsonObject.value("versionFree").toArray();
    QStringList allowedVersions;
    for(const QJsonValue &value: versionArray)
    {
        allowedVersions.append(value.toString());
    }
    this->latestVersion = allowedVersions.isEmpty()?"":allowedVersions.last();

    //Replace url versions
    versionJson.replace("vx.x", this->latestVersion.toUtf8());
    versionJsonObject = QJsonDocument::fromJson(versionJson).object();
    const QUrl downloadUrl(versionJsonObject.value("downloadUrl").toString());


    QSettings settings;
    QString remindedVersion = settings.value("version", "").toString();

    emit pDebug("VERSION: " + VERSION + " - RemindedVersion: " + remindedVersion +
                " - LatestVersion: " + latestVersion + " - AllowedVersions: " + allowedVersions.join(","));

    if(remindedVersion.isEmpty())
    {
        remindedVersion = VERSION;
    }


    //This version isn't allowed anymore: download the latest or quit
    if(!allowedVersions.contains(VERSION))
    {
        emit pDebug("Arena Dude " + latestVersion + " is available for download. This version is no longer supported.");

        QMessageBox msgBox(static_cast<QMainWindow*>(this->parent()));
        msgBox.setText("Arena Dude " + latestVersion + " is available for download.\nThis version is no longer supported.");
        msgBox.setWindowTitle(tr("New version"));
        msgBox.setIcon(QMessageBox::Information);
        QPushButton *button1 = msgBox.addButton("Download", QMessageBox::ActionRole);
        msgBox.addButton("Exit", QMessageBox::ActionRole);

        msgBox.exec();

        if(msgBox.clickedButton() == button1)   QDesktopServices::openUrl(downloadUrl);
        static_cast<QMainWindow*>(this->parent())->close();
        qApp->quit();       //The app doesn't quit on its last window closing (main)
    }
    else if(remindedVersion != latestVersion)
    {
        if(VERSION == latestVersion)
        {
            settings.setValue("version", VERSION);
            emit pDebug("Arena Dude is up-to-date.");
            this->deleteLater();
        }
        else
        {
            emit pDebug("Arena Dude " + latestVersion + " is available for download.");

            QMessageBox msgBox(static_cast<QMainWindow*>(this->parent()));
            msgBox.setText("Arena Dude " + latestVersion + " is available for download.");
            msgBox.setWindowTitle(tr("New version"));
            msgBox.setIcon(QMessageBox::Information);
            QPushButton *button1 = msgBox.addButton("Download", QMessageBox::ActionRole);
            msgBox.addButton("Remind me later", QMessageBox::ActionRole);
            QPushButton *button3 = msgBox.addButton("Don't remind me", QMessageBox::ActionRole);

            msgBox.exec();

            if(msgBox.clickedButton() == button1)       QDesktopServices::openUrl(downloadUrl);
            else if(msgBox.clickedButton() == button3)  settings.setValue("version", latestVersion);
            this->deleteLater();
        }
    }
    else if(VERSION != latestVersion)
    {
        emit pDebug("Arena Dude " + latestVersion + " is available for download.");
        this->deleteLater();
    }
    else
    {
        emit pDebug("Arena Dude is up-to-date.");
        this->deleteLater();
    }

    if(newVersion && VERSION == latestVersion)  showVersionLog(versionJsonObject.value("log").toString());
}


void VersionChecker::showVersionLog(QString changesLog)
{
    if(changesLog.isEmpty())    return;

    QMessageBox msgBox(static_cast<QMainWindow*>(this->parent()));
    msgBox.setText(changesLog);
    msgBox.setWindowTitle(VERSION + " changes");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.addButton(QMessageBox::Ok);

    msgBox.exec();
}
