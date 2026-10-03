#ifndef GAMEWATCHER_H
#define GAMEWATCHER_H

#include <QObject>
#include <QString>
#include "Cards/deckcard.h"
#include "utility.h"


class GameResult
{
public:
    bool isFirst, isWinner;
    QString playerHero, enemyHero, enemyName;
};


class GameWatcher : public QObject
{
    Q_OBJECT
public:
    GameWatcher(QObject *parent = nullptr);
    ~GameWatcher();

private:
    QStringList deckSnapshot;           //Codes of the deck snapshot being read
    bool deckSnapshotSync = false, redraftBeginSeen = false;
    enum PowerState { noGame, heroType1State, heroPower1State, heroType2State, mulliganState, inGameState };
    enum ArenaState { noDeckRead, deckRead, readingDeck };

//Variables
private:
    QString playerTag;
    PowerState powerState;
    ArenaState arenaState;
    LoadingScreenState loadingScreenState;
    QString hero1, hero2, name1, name2, firstPlayer, winnerPlayer;
    int playerID;
    QRegularExpressionMatch *match;
    bool mulliganEnemyDone, mulliganPlayerDone;
    bool spectating, tied;



//Metodos
private:
    void createGameResult();
    void processLoadingScreen(QString &line, qint64 numLine);
    void processAsset(QString &line, qint64 numLine);
    void processArena(QString &line, qint64 numLine);
    void processPower(QString &line, qint64 numLine);
    void processPowerHero(QString &line, qint64 numLine);
    void processPowerMulligan(QString &line, qint64 numLine);
    void processZone(QString &line, qint64 numLine);
    void startReadingDeck();
    void emitDeckSnapshot();
    void endReadingDeck();
    QString getNamePreSharp(QString name);

public:
    void reset();
    LoadingScreenState getLoadingScreen();

signals:
    void newGameResult(GameResult gameResult, LoadingScreenState loadingScreen);
    void newArena(QString hero);
    void continueDraft();
    void checkRedraft();
    void redraft();
    void arenaChoosingHeroe();
    void inRewards();
    void deckSnapshotRead(QStringList codes);
    void arenaRetired();
    void newDeckCard(QString card);
    void startGame();
    void endGame(bool playerWon=false, bool playerUnknown=true);
    void enterArena();
    void leaveArena();
    void enemySecretPlayed();
    void needResetDeck();
    void heroDraftDeck(QString hero="");
    void activeDraftDeck();
    void pickCard(QString code);
    void arenaDeckRead();
    void pDebug(QString line, qint64 numLine, DebugLevel debugLevel=Normal, QString file="GameWatcher");

public slots:
    void processLogLine(LogComponent logComponent, QString line, qint64 numLine, qint64 logSeek);
    void setDeckRead(bool value=true);
};

#endif // GAMEWATCHER_H
