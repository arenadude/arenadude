#include "winratesdownloader.h"
#include "QtConcurrent/qtconcurrentrun.h"
#include "qfileinfo.h"
#include "qjsonarray.h"
#include "qjsondocument.h"
#include "qnetworkreply.h"
#include <QRegularExpression>


float WinratesDownloader::heroScores[NUM_HEROS] = {0};
int WinratesDownloader::heroGames[NUM_HEROS] = {0};


//Arena winrate of the class, 0 if unknown
float WinratesDownloader::getHeroScore(int classOrder)
{
    if(classOrder < 0 || classOrder >= NUM_HEROS)   return 0;
    return heroScores[classOrder];
}


//Games behind getHeroScore, -1 if unknown
int WinratesDownloader::getHeroGames(int classOrder)
{
    if(classOrder < 0 || classOrder >= NUM_HEROS)   return -1;
    return heroGames[classOrder];
}


WinratesDownloader::WinratesDownloader(QObject *parent) : QObject(parent)
{
    fireDataThreads = 0;
    fireWRMap = new QMap<QString, float>[NUM_HEROS];
    fireSamplesMap = new QMap<QString, int>[NUM_HEROS];

    match = new QRegularExpressionMatch();

    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(replyFinished(QNetworkReply*)));
}

WinratesDownloader::~WinratesDownloader()
{
    delete networkManager;
    delete match;

    //Delete Fire maps
    if(fireWRMap != nullptr)        delete[] fireWRMap;
    if(fireSamplesMap != nullptr)   delete[] fireSamplesMap;
}


void WinratesDownloader::waitFinishThreads()
{
    for(int i=0; i<NUM_HEROS; i++)
    {
        if(futureFire[i].isRunning())           futureFire[i].waitForFinished();
    }
}


int WinratesDownloader::url2classOrder(QString url)
{
    int classOrder = -1;
    static const auto re = QRegularExpression(QString(FIRE_CARDS_URL) + "(\\w+)\\.gz\\.json");
    if(url.contains(re, match))
    {
        QString hero = match->captured(1);
        classOrder = Utility::className2classOrder(hero);
    }
    return classOrder;
}


void WinratesDownloader::replyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    QString fullUrl = reply->url().toString();

    if(reply->error() != QNetworkReply::NoError)
    {
        emit pDebug(reply->url().toString() + " --> Failed.");

        if(fullUrl.startsWith(FIRE_CARDS_URL))
        {
            int classOrder = url2classOrder(fullUrl);
            if(classOrder == -1)    emit pDebug("ERROR: Fail retrieving class from url:" + fullUrl);
            else                    localFireCards(classOrder);
        }
        else if(fullUrl == FIRE_CLASSES_URL)
        {
            localHeroesWinrate();
        }
    }
    else
    {
        //Fire Cards Winrate/Samples
        if(fullUrl.startsWith(FIRE_CARDS_URL))
        {
            emit pDebug("Fire cards --> Download Success from: " + fullUrl);

            int classOrder = url2classOrder(fullUrl);
            if(classOrder == -1)
            {
                emit pDebug("ERROR: Fail retrieving class from url:" + fullUrl);
                return;
            }

            QString filename = "fireCards" + QString::number(classOrder) + ".json";
            QByteArray jsonData = reply->readAll();
            Utility::dumpOnFile(jsonData, Utility::extraPath() + "/" + filename);
            startProcessFireCards(QJsonDocument::fromJson(jsonData).object(), classOrder);
        }
        //Fire Heroes Winrate
        else if(fullUrl == FIRE_CLASSES_URL)
        {
            emit pDebug("Heroes winrate --> Download Success.");
            QByteArray jsonData = reply->readAll();
            Utility::dumpOnFile(jsonData, Utility::extraPath() + "/" + FIRE_CLASSES_FILE);
            processHeroesWinrate(QJsonDocument::fromJson(jsonData).object());
        }
    }
}


//Show threads progres
//Class winrates of the arena from Firestone
void WinratesDownloader::initHeroesWinrate()
{
    QFileInfo fi(Utility::extraPath() + "/" + FIRE_CLASSES_FILE);
    if(fi.exists() && (fi.lastModified().addDays(1)>QDateTime::currentDateTime()))
    {
        localHeroesWinrate();
    }
    else
    {
        emit pDebug("Heroes winrate --> Download from: " + QString(FIRE_CLASSES_URL));
        networkManager->get(QNetworkRequest(QUrl(FIRE_CLASSES_URL)));
    }
}


void WinratesDownloader::localHeroesWinrate()
{
    emit pDebug(QStringLiteral("Heroes winrate --> Use local %1").arg(FIRE_CLASSES_FILE));

    QFile file(Utility::extraPath() + "/" + FIRE_CLASSES_FILE);
    if(!file.open(QIODevice::ReadOnly))
    {
        emit pDebug(QStringLiteral("ERROR: Failed to open %1").arg(FIRE_CLASSES_FILE));
        return;
    }
    QByteArray jsonData = file.readAll();
    file.close();
    processHeroesWinrate(QJsonDocument::fromJson(jsonData).object());
}


//Each class has one entry per hero power seen with it (Discover effects);
//the one with the most games is the class with its own hero power.
void WinratesDownloader::processHeroesWinrate(const QJsonObject &jsonObject)
{
    float heroScores[NUM_HEROS] = {0};
    int heroGames[NUM_HEROS] = {0};
    const QJsonArray stats = jsonObject["stats"].toArray();

    for(const QJsonValue &jv: stats)
    {
        const QJsonObject jo = jv.toObject();
        int classOrder = Utility::className2classOrder(jo["playerClass"].toString());
        int games = jo["totalGames"].toInt();
        if(classOrder < 0 || classOrder >= NUM_HEROS || games <= heroGames[classOrder])    continue;

        heroGames[classOrder] = games;
        heroScores[classOrder] = round(jo["totalsWins"].toDouble() / games * 1000)/10.0;
    }

    emit pDebug("Heroes winrate (Firestone) ready.");
    for(int i=0; i<NUM_HEROS; i++)
    {
        WinratesDownloader::heroScores[i] = heroScores[i];
        WinratesDownloader::heroGames[i] = heroGames[i];
    }
    emit readyHeroesWinrate();
}


void WinratesDownloader::initWRCards()
{
    fireDataThreads = NUM_HEROS;
    initFireCards();
}


void WinratesDownloader::initFireCards()
{
    for(int i=0; i<NUM_HEROS; i++)
    {
        connect(&futureFire[i], &QFutureWatcher<FireData>::finished, this,[this,i]()
        {
            // emit pDebug("Fire cards (" + Utility::classOrder2classLName(i) + ") --> Thread end.");
            FireData fireData = futureFire[i].result();
            this->fireWRMap[i] = fireData.fireWRMap;
            this->fireSamplesMap[i] = fireData.fireSamplesMap;
            fireDataThreads--;
            if(fireDataThreads == 0)
            {
                emit pDebug("Fire cards (WR/Samples) ready.");
                emit readyFireWRMap(fireWRMap);
                emit readyFireSamplesMap(fireSamplesMap);
            }
        });


        QString hero = Utility::classOrder2classLName(i);
        QString filename = "fireCards" + QString::number(i) + ".json";
        QFileInfo fi(Utility::extraPath() + "/" + filename);
        if(fi.exists() && (fi.lastModified().addDays(1)>QDateTime::currentDateTime()))
        {
            localFireCards(i);
        }
        else
        {
            QString url = FIRE_CARDS_URL + hero + ".gz.json";
            emit pDebug("Fire cards --> Download from: " + QString(url));
            networkManager->get(QNetworkRequest(QUrl(url)));
        }
    }
}


void WinratesDownloader::localFireCards(const int classOrder)
{
    QString filename = "fireCards" + QString::number(classOrder) + ".json";
    emit pDebug("Fire cards --> Use local " + filename);

    QFile file(Utility::extraPath() + "/" + filename);
    if(!file.open(QIODevice::ReadOnly))
    {
        emit pDebug("ERROR: Failed to open " + filename);
        fireDataThreads--;
        return;
    }
    QByteArray jsonData = file.readAll();
    file.close();
    startProcessFireCards(QJsonDocument::fromJson(jsonData).object(), classOrder);
}


void WinratesDownloader::startProcessFireCards(const QJsonObject &jsonObject, const int classOrder)
{
    if(futureFire[classOrder].isRunning())  return;

    const QJsonArray &data = jsonObject.value("stats").toArray();

    QFuture<FireData> future = QtConcurrent::run([data]()->FireData{
        FireData fireData;

        for(const QJsonValue &card: data)
        {
            QJsonObject cardObject = card.toObject();
            QString code = cardObject.value("cardId").toString();
            QJsonObject cardStatsObject = cardObject.value("stats").toObject();
            int samples = cardStatsObject.value("decksWithCard").toInt();
            int wins = cardStatsObject.value("decksWithCardThenWin").toInt();
            float wr = round((wins/(float)samples) * 1000)/10.0;
            fireData.fireSamplesMap.insert(code, samples);
            fireData.fireWRMap.insert(code, wr);
        }
        return fireData;
    });
    futureFire[classOrder].setFuture(future);
}



