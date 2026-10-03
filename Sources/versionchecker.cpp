#include "versionchecker.h"
#include <QNetworkCookieJar>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QtConcurrent/QtConcurrent>
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

    //ArenaDude.new run - WIN 10 updater
    connect(&futureNewAppReplace, SIGNAL(finished()), this, SLOT(finishNewAppReplace()));
    if(isNewApp())    startNewAppReplace();
    removeOldNewVersion();
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
        QString fullUrl = reply->url().toString();

        //Redirect
        if(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 302)
        {
            QByteArray location = reply->rawHeader("Location");
            emit pDebug("Redirect to --> " + location);
            networkManager->get(QNetworkRequest(QUrl(location)));
        }

        //Check version
        else if(fullUrl == VERSION_URL)
        {
            checkUpdate(reply->readAll());
        }

        //New version downloaded
        else
        {
            emit pDebug(latestVersion + " downloaded.");
            saveRestart(reply->readAll());
        }
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


    QSettings settings;
    QString remindedVersion = settings.value("version", "").toString();

    emit pDebug("VERSION: " + VERSION + " - RemindedVersion: " + remindedVersion +
                " - LatestVersion: " + latestVersion + " - AllowedVersions: " + allowedVersions.join(","));

    if(remindedVersion.isEmpty())
    {
        remindedVersion = VERSION;
    }


    if(!allowedVersions.contains(VERSION))
    {
        emit pDebug("Arena Dude " + latestVersion + " is available for download.");

        QMessageBox msgBox(static_cast<QMainWindow*>(this->parent()));
        msgBox.setText("Arena Dude " + latestVersion + " is available for download.");
        msgBox.setWindowTitle(tr("New version"));
        msgBox.setIcon(QMessageBox::Information);
        QPushButton *button1 = msgBox.addButton("Update", QMessageBox::ActionRole);
        msgBox.addButton("Exit", QMessageBox::ActionRole);

        msgBox.exec();

        if(msgBox.clickedButton() == button1)
        {
            downloadLatestVersion(versionJsonObject);
        }
        else
        {
            static_cast<QMainWindow*>(this->parent())->close();
        }
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
            QPushButton *button1 = msgBox.addButton("Update", QMessageBox::ActionRole);
            QPushButton *button2 = msgBox.addButton("Remind me later", QMessageBox::ActionRole);
            QPushButton *button3 = msgBox.addButton("Don't remind me", QMessageBox::ActionRole);

            msgBox.exec();

            if(msgBox.clickedButton() == button1)
            {
                downloadLatestVersion(versionJsonObject);
            }
            else if(msgBox.clickedButton() == button2)
            {
                this->deleteLater();
            }
            else if(msgBox.clickedButton() == button3)
            {
                settings.setValue("version", latestVersion);
                this->deleteLater();
            }
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
    QMessageBox msgBox(static_cast<QMainWindow*>(this->parent()));
    msgBox.setText(changesLog);
    msgBox.setWindowTitle(VERSION + " changes");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.addButton(QMessageBox::Ok);

    msgBox.exec();
}


void VersionChecker::downloadLatestVersion(const QJsonObject &versionJsonObject)
{
    QString binaryUrl = "";


        binaryUrl = versionJsonObject.value("macUrl").toString();


    if(!binaryUrl.isEmpty())
    {
        emit pDebug("New binary --> Download from: " + binaryUrl);
        networkManager->get(QNetworkRequest(QUrl(binaryUrl)));
    }
    else                        this->deleteLater();
}


void VersionChecker::saveRestart(const QByteArray &data)
{
    emit pDebug("New binary --> Download Success.");


    saveRestartOld(data);
}


void VersionChecker::saveRestartOld(const QByteArray &data)
{
    emit pDebug("Using ArenaDude.old renaming.");

    QString runningBinaryName = QCoreApplication::applicationFilePath().split("/").last();
    QString runningBinaryPath = Utility::appPath() + "/" + runningBinaryName;

    QFile appFile(runningBinaryPath);
    QFile::Permissions permissions = appFile.permissions();

    emit pDebug(runningBinaryPath + " rename " + Utility::dataPath() + "/ArenaDude.old");
    appFile.rename(Utility::dataPath() + "/ArenaDude.old");

    Utility::dumpOnFile(data, Utility::dataPath() + "/binaryTemp.zip");
    Utility::unZip(Utility::dataPath() + "/binaryTemp.zip", Utility::appPath());
    QFile zipFile(Utility::dataPath() + "/binaryTemp.zip");
    zipFile.remove();

    emit pDebug("Extract ArenaDude on " + Utility::appPath());

    QFile::setPermissions(runningBinaryPath, permissions);

    emit pDebug("Start downloaded ArenaDude...");

    QProcess::startDetached(qApp->arguments()[0], qApp->arguments());
    static_cast<QMainWindow*>(this->parent())->close();
}


void VersionChecker::removeOldNewVersion()
{
    QFile appOld(Utility::dataPath() + "/ArenaDude.old");
    if(appOld.exists())
    {
        qDebug() << Utility::dataPath() + "/ArenaDude.old" << "removed.";
        appOld.remove();
    }

    QFile appNew(Utility::dataPath() + "/ArenaDude.new");
    if(appNew.exists())
    {
        qDebug() << Utility::dataPath() + "/ArenaDude.new" << "removed.";
        appNew.remove();
    }
}


bool VersionChecker::isNewApp()
{
    QString runningBinaryName = QCoreApplication::applicationFilePath().split("/").last();
    return runningBinaryName == "ArenaDude.new";
}


void VersionChecker::newAppReplace()
{
    QSettings settings;
    QString runningBinaryPath = settings.value("runningBinaryPath", "").toString();
    QString runningBinaryName = runningBinaryPath.split("/").last();
    QString dataBinaryPath = Utility::dataPath() + "/" + runningBinaryName;

    qDebug() << "ArenaDude.new running...";

    QFile appFile(runningBinaryPath);
    if(!appFile.exists())   return;
    while(!appFile.remove())    QThread::sleep(1);

    qDebug() << runningBinaryPath << "removed.";

    QFile appDataFile(dataBinaryPath);
    while(!appDataFile.rename(runningBinaryPath))    QThread::sleep(1);

    qDebug() << dataBinaryPath << "renamed to" << runningBinaryPath;
}


void VersionChecker::startNewAppReplace()
{
    if(!futureNewAppReplace.isRunning()) futureNewAppReplace.setFuture(QtConcurrent::run(&VersionChecker::newAppReplace, this));
}
void VersionChecker::finishNewAppReplace()
{
    emit pDebug("ArenaDude replaced by downloaded version.");
}
