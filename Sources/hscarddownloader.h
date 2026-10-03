#ifndef HSCARDDOWNLOADER_H
#define HSCARDDOWNLOADER_H

#include "utility.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMap>

#define HEARTHSIM_CARDS_URL QString("https://art.hearthstonejson.com/v1/render/latest/enUS/256x/")
#define HEARTHPWN_CARDS_GOLDEN_URL QString("https://cards.hearthpwn.com/enUS/anims/")
#define HEARTHPWN_CARDS_PLAIN_URL QString("https://cards.hearthpwn.com/enUS/")
#define MAX_DOWNLOADS 10


class DownloadingCard
{
public:
    QString code = "";
    bool fallback = false;
};


class HSCardDownloader : public QObject
{
    Q_OBJECT
public:
    HSCardDownloader(QObject *parent);
    ~HSCardDownloader();

//Variables
private:
    QNetworkAccessManager *networkManager;
    QMap<QNetworkReply *, DownloadingCard> gettingWebCards;
    QString lang;
    QList<DownloadingCard> pendingDownloads;


//Metodos
private:
    void downloadWebImage(DownloadingCard downCard, bool force=false);

public:
    void downloadWebImage(QString code, bool force=false, bool fallback=false);
    void setLang(QString value);

signals:
    void downloaded(QString code);
    void missingOnWeb(QString code);
    void allCardsDownloaded();
    void pDebug(QString line, DebugLevel debugLevel=Normal, QString file="HSCardDownloader");

public slots:
    void saveWebImage(QNetworkReply * reply);

private slots:
    void forceNextDownload();
};

#endif // HSCARDDOWNLOADER_H
