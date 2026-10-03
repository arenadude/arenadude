#include "arenahandler.h"
#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>


ArenaHandler::ArenaHandler(QObject *parent) : QObject(parent)
{
}


bool ArenaHandler::isCompleteArena(int wins, int losses)
{
    return (losses == 3 && wins < 12) || (losses < 3 && wins == 12);
}


void ArenaHandler::loadStatsJsonFile()
{
    QFile jsonFile(Utility::arenaStatsPath() + "/" + ARENA_STATS_FILE);
    if(!jsonFile.exists())
    {
        emit pDebug(QString(ARENA_STATS_FILE) + " doesn't exists.");
        return;
    }
    if(!jsonFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        emit pDebug("Failed to load " + QString(ARENA_STATS_FILE) + " from disk.", DebugLevel::Error);
        statsFileOk = false;
        return;
    }
    statsJson = QJsonDocument::fromJson(jsonFile.readAll()).object();
    jsonFile.close();
    emit pDebug("Loaded " + QString::number(statsJson.count()) + " entries from " + ARENA_STATS_FILE + ".");

    //A run finished while the tracker was closed
    if(statsJson.contains("current"))
    {
        QJsonObject objArena = statsJson["current"].toObject();
        if(objArena["wins"].toInt() > 11 || objArena["losses"].toInt() > 2)
        {
            archiveCurrent();
            saveStatsJsonFile();
        }
    }
}


void ArenaHandler::saveStatsJsonFile()
{
    if(!statsFileOk)    return;

    QFile jsonFile(Utility::arenaStatsPath() + "/" + ARENA_STATS_FILE);
    if(!jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        emit pDebug("Failed to create " + QString(ARENA_STATS_FILE) + " on disk.", DebugLevel::Error);
        return;
    }
    jsonFile.write(QJsonDocument(statsJson).toJson());
    jsonFile.close();
    emit pDebug(QString(ARENA_STATS_FILE) + " updated.");
}


QString ArenaHandler::lastGameDate()
{
    return statsJson["extra"].toObject()["lastGame"].toString();
}


void ArenaHandler::setLastGameDate()
{
    QJsonObject objExtra = statsJson["extra"].toObject();
    objExtra["lastGame"] = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm");
    statsJson["extra"] = objExtra;
}


//The date of a finished run, a random time of its day if another run has it
QString ArenaHandler::getUniqueDate(QString date)
{
    if(date.isEmpty())  date = QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm");
    if(!statsJson.contains(date))   return date;

    QDateTime day = QDateTime::fromString(QDateTime::fromString(date, "yyyy.MM.dd hh:mm").toString("yyyy.MM.dd"), "yyyy.MM.dd");
    do
    {
        date = day.addSecs(QRandomGenerator::global()->bounded(86400)).toString("yyyy.MM.dd hh:mm");
    }
    while(statsJson.contains(date));
    return date;
}


//The current run goes to the history under its last game's date
void ArenaHandler::archiveCurrent()
{
    if(!statsJson.contains("current"))     return;
    statsJson[getUniqueDate(lastGameDate())] = statsJson["current"].toObject();
    statsJson.remove("current");
}


void ArenaHandler::newCurrent(const QString &hero, int wins, int losses)
{
    archiveCurrent();
    QJsonObject objArena;
    objArena["hero"] = hero;
    objArena["wins"] = wins;
    objArena["losses"] = losses;
    statsJson["current"] = objArena;
    setLastGameDate();
}


void ArenaHandler::setLogsCaughtUp()
{
    logsCaughtUp = true;
}


void ArenaHandler::newArena(QString hero)
{
    if(hero.isEmpty())
    {
        emit pDebug("New arena with empty hero.");
        return;
    }
    //At startup the logs replay the end of the last draft: when the current run is that draft's (same hero,
    //not finished) it's kept, or a restart in the middle of a run would start it again from 0-0
    const QJsonObject current = statsJson["current"].toObject();
    if(!logsCaughtUp && statsJson.contains("current") && current["hero"].toString() == hero &&
            !isCompleteArena(current["wins"].toInt(), current["losses"].toInt()))
    {
        emit pDebug("New arena: replayed draft end of the current run. Keep it.");
        return;
    }

    emit pDebug("New arena. Hero: " + hero);
    newCurrent(hero, 0, 0);
    saveStatsJsonFile();
}


void ArenaHandler::newGameResult(GameResult gameResult, LoadingScreenState loadingScreen)
{
    if(loadingScreen != arena)  return;

    const QJsonObject current = statsJson["current"].toObject();
    int wins = 0, losses = 0;
    if(!statsJson.contains("current") || current["hero"].toString() != gameResult.playerHero)
    {
        emit pDebug("Arena game with no current run of this hero: a new run.");
        newCurrent(gameResult.playerHero, 0, 0);
    }
    else
    {
        wins = current["wins"].toInt();
        losses = current["losses"].toInt();
    }

    if(gameResult.isWinner) wins++;
    else                    losses++;
    QJsonObject objArena = statsJson["current"].toObject();
    objArena["wins"] = wins;
    objArena["losses"] = losses;
    statsJson["current"] = objArena;
    setLastGameDate();

    emit pDebug(QStringLiteral("Arena run: %1-%2.").arg(wins).arg(losses));
    emit arenaRecordChanged(wins, losses, gameResult.isWinner);
    saveStatsJsonFile();
}


//The deck averages of the run, from its draft
void ArenaHandler::setCurrentAvgScore(int avgHA, float avgFire, QString heroLog)
{
    QJsonObject objArena = statsJson["current"].toObject();
    if(!statsJson.contains("current") || objArena["hero"].toString() != heroLog)
    {
        emit pDebug("Avoid Set AvgScore: no current run of this hero.");
        return;
    }
    objArena["avgHA"] = avgHA;
    objArena["avgFire"] = round(avgFire * 10)/10.0;
    statsJson["current"] = objArena;
    saveStatsJsonFile();
    emit pDebug("Set AvgScore: HA:" + QString::number(avgHA) + " - Fire:" + QString::number(avgFire));
}
