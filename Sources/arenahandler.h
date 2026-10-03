#ifndef ARENAHANDLER_H
#define ARENAHANDLER_H

#include "gamewatcher.h"
#include "utility.h"
#include <QObject>
#include <QJsonObject>

#define ARENA_STATS_FILE    "ArenaDudeStats.json"


//The record of the arena runs, kept in ArenaDudeStats.json: the run being played is "current", finished runs
//are kept under their last game's date ("yyyy.MM.dd hh:mm"), "extra" holds the last game's date
class ArenaHandler : public QObject
{
    Q_OBJECT
public:
    ArenaHandler(QObject *parent);
    void loadStatsJsonFile();       //After pDebug is connected

//Variables
private:
    QJsonObject statsJson;
    bool statsFileOk = true;    //A file that exists but can't be read is never overwritten
    bool logsCaughtUp = false;

//Metodos
private:
    void saveStatsJsonFile();
    void archiveCurrent();
    void newCurrent(const QString &hero, int wins, int losses);
    QString getUniqueDate(QString date);
    QString lastGameDate();
    void setLastGameDate();
    static bool isCompleteArena(int wins, int losses);

signals:
    void arenaRecordChanged(int wins, int losses, bool lastWon);     //After each game of the current arena run
    void pDebug(QString line, DebugLevel debugLevel=Normal, QString file="ArenaHandler");

public slots:
    //MainWindow
    void newGameResult(GameResult gameResult, LoadingScreenState loadingScreen);

    //DraftHandler
    void newArena(QString hero);
    void setLogsCaughtUp();
    void setCurrentAvgScore(int avgHA, float avgFire, QString heroLog);
};

#endif // ARENAHANDLER_H
