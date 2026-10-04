#ifndef WINRATESDOWNLOADER_H
#define WINRATESDOWNLOADER_H

#include "Sources/utility.h"
#include "qfuturewatcher.h"
#include <QObject>
#include <QNetworkAccessManager>

#define FIRE_CARDS_URL "https://static.zerotoheroes.com/api/arena/stats/cards/arena-underground/last-patch/"
#define FIRE_CLASSES_URL "https://static.zerotoheroes.com/api/arena/stats/classes/arena-underground/last-patch/overview.gz.json"

#define FIRE_CLASSES_FILE "fireClasses.json"

//The opponent's top cards at the mulligan: only cards drawn enough times and in enough of the class's decks
#define TOP_CARDS_MIN_DRAWN     200
#define TOP_CARDS_MIN_SHARE     0.03
//A card as good as its class still counts if it's common: compared with the class's winrate minus this
#define TOP_CARDS_WINRATE_SLACK 0.02


//A card's games in the decks of one class (Firestone)
class FireCardStats
{
public:
    int copies = 0;         //Copies in the starting decks (inStartingDeck): 2 copies count twice
    int decks = 0;          //Decks with the card
    int drawn = 0, drawnWins = 0;
};


class FireData
{
public:
    QMap<QString, float> fireWRMap;
    QMap<QString, int> fireSamplesMap;
    QMap<QString, FireCardStats> fireStatsMap;
};


//A card the opponent's class often plays and wins with
struct TopCard
{
    QString code;
    float deckShare;        //% of the class's decks with it
    float drawnWinrate;     //% of games won when drawn
};


class WinratesDownloader : public QObject
{
    Q_OBJECT
public:
    WinratesDownloader(QObject *parent);
    ~WinratesDownloader();

//Variables
private:
    QNetworkAccessManager *networkManager;
    QRegularExpressionMatch *match;
    QFutureWatcher<FireData> futureFire[NUM_HEROS];
    int fireDataThreads;
    QMap<QString, float> *fireWRMap;
    QMap<QString, int> *fireSamplesMap;
    QMap<QString, FireCardStats> fireStatsMap[NUM_HEROS];
    static float heroScores[NUM_HEROS];     //Arena winrate of each class (Firestone)
    static int heroGames[NUM_HEROS];


//Metodos
private:
    void initFireCards();
    void localHeroesWinrate();
    void localFireCards(const int classOrder);
    void startProcessFireCards(const QJsonObject &jsonObject, const int classOrder);
    void processHeroesWinrate(const QJsonObject &jsonObject);
    int url2classOrder(QString url);

public:
    void initWRCards();
    void initHeroesWinrate();
    void waitFinishThreads();
    static float getHeroScore(int classOrder);
    static int getHeroGames(int classOrder);
    QList<TopCard> getTopCards(int classOrder, int count);

signals:
    void pDebug(QString line, DebugLevel debugLevel=Normal, QString file="WinratesDownloader");
    void readyFireWRMap(QMap<QString, float> *fireWRMap);
    void readyFireSamplesMap(QMap<QString, int> *fireSamplesMap);
    void readyHeroesWinrate();

private slots:
    void replyFinished(QNetworkReply *reply);
};

#endif // WINRATESDOWNLOADER_H
