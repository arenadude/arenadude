#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "Sources/winratesdownloader.h"
#include "logloader.h"
#include "gamewatcher.h"
#include "hscarddownloader.h"
#include "deckhandler.h"
#include "arenahandler.h"
#include "drafthandler.h"
#include "Widgets/cardwindow.h"
#include "Widgets/mascotwindow.h"
#include <QMainWindow>
#include <QJsonObject>

#define EXTRA_URL AT_REPO_RAW_URL "/Extra"
#define IMAGES_URL AT_REPO_RAW_URL "/Images"
#define HA_URL AT_REPO_RAW_URL "/HearthArena"
#define ARENA_URL AT_REPO_RAW_URL "/Arena"
#define CARDS_URL AT_REPO_RAW_URL "/CardsJson"


//TODO: the Patreon page and the Discord server, once they exist
#define MASCOT_SUPPORT_WINS 4   //The support ask comes after a win, on the Ready Up screen, from these wins on:
                                //a normal arena run ends at 5 wins, so 5 came too late
#define MASCOT_SOLID_RUN_WINS 5 //A run ending with these wins or more is praised
#define MASCOT_SUPPORT_URL "https://www.patreon.com/"
#define SCREEN_RECORDING_SETTINGS_URL "x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"
#define MULLIGAN_TOP_CARDS 10        //Cards shown at the mulligan
#define MASCOT_DISCORD_URL "https://discord.gg/"

class MainWindow : public QMainWindow
{
    Q_OBJECT

//Constructor
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() Q_DECL_OVERRIDE;


//Variables
private:
    LogLoader *logLoader;
    GameWatcher *gameWatcher;
    HSCardDownloader *cardDownloader;
    WinratesDownloader *winratesDownloader;
    DeckHandler *deckHandler;
    ArenaHandler *arenaHandler;
    DraftHandler * draftHandler;
    CardWindow *cardWindow;
    QMap<QString, QJsonObject> cardsJson;
    QFile* atLogFile;
    MascotWindow *mascotWindow;
    int mascotRedraftScreenShown = 0;           //RedraftScreen the mascot talks about
    bool mascotSupportAsked = false;            //Once per arena run
    bool mascotInGame = false;
    bool mascotSaysMulligan = false;            //The bubble shows the opponent's top cards, hidden after the mulligan
    bool splashOpen = false, initDone = false;  //The mascot shows after both
    bool mascotNoRun = false;                   //The last run ended (rewards screen) and no new draft yet
    bool mascotRetired = false;
    bool mascotRewardsRetired = false;          //The run of the rewards screen being read ended by a retire
    QString mascotLastStatus;                   //The draft status behind the current status line
    QHash<QString, qint64> mascotStatusShownAt; //When each draft status was last shown (anti flip-flop)
    int mascotSecretsSeen = 0;                  //Enemy secrets since the app started
    bool mascotSaysStatus = false;              //The bubble shows a draft status, cleared with it
    bool mascotSaysSystemDialog = false;        //A system dialog covers Hearthstone (checkSystemDialog)
    bool mascotSaysAdvice = false;              //The bubble shows the pick advice: only a new pick or a problem replaces it
    bool mascotLive = false;                    //Game events replayed from the logs at startup are ignored
    bool mascotLastWon = false;
    int mascotLastLosses = 0;
    QNetworkAccessManager *networkManager;
    QStringList allCardsDownloadList;
    int allCardsDownloadTotal = 0;
    bool allCardsDownloadStarted = false;
    //Gestionan si es necesario bajar todas las cartas usadas en arena debido a que el directorio de cartas se haya borrado
    //o haya una nueva version de tier list (rotacion sets)
    bool cardsJsonLoaded, arenaSetsLoaded, allCardsDownloadNeeded;


//Metodos
public:
    void setSplashOpen();
    void splashClosed();
    LoadingScreenState getLoadingScreen();

private:
    void initVariables();
    void logPlatform();
    void createLogLoader();
    void createArenaHandler();
    void createGameWatcher();
    void createCardWindow();
    static QColor mascotRarityColor(const QString &code);
    static QColor mascotClassColor(int classOrder);
    static QStringList mascotGoodLuckLines();
    void createCardDownloader();
    void createWinratesDownloader();
    void createDeckHandler();
    void createDraftHandler();
    void createVersionChecker();
    void readSettings();
    QString getHSLanguage();
    void createCardsJsonMap(QByteArray &jsonData);
    void createLogFile();
    void closeLogFile();
    void createDataDir();
    void createNetworkManager();
    void initCardsJson();
    void removeHSCards(bool forceRemove = false);
    void removeExtraAndHistograms();
    void removeHistograms();
    void checkCardsJsonVersion(QString cardsJsonVersion);
    void checkFirstRunNewVersion();
    void updateProgressAllCardsDownload(QString code);
    void downloadExtraFile(QString nameFile);
    void downloadExtraFiles();
    void downloadHearthArenaVersion();
    void downloadHearthArenaJson(int version);
    void downloadArenaVersion();
    void checkArenaVersionJson(const QJsonObject &jsonObject);
    void initHeroesWinrate();
    void checkArenaCards();
    void downloadAllArenaCodes(const QStringList &codeList);
    void initWRCards();
    void downloadCardsJsonVersion();
    void downloadCardsJson(int version);

//Signals
signals:
    //The first run downloads every arena card image: the splash shows it, then closes when ready
    void startupProgress(int done, int total);
    void startupReady();


//Slots
public slots:
    //GameWatcher
    void resetDeck(bool deckRead=false);
    void resetDeckDontRead();

    //Multi Handlers
    bool checkCardImage(QString code);

    //HSCardDownloader
    void redrawDownloadedCardImage(QString code);

    //MainWindow
    void pDebug(QString line, DebugLevel debugLevel=Normal, QString file="MainWindow");
    void pDebug(QString line, qint64 numLine, DebugLevel debugLevel, QString file);


private slots:
    void closeApp();
    void createMascotWindow();
    void mascotDraftStatus(QString text);
    void checkSystemDialog();
    void mascotStartGame();
    void mascotEndGame(bool playerWon, bool playerUnknown);
    void mascotEnemySecret();
    void mascotMulligan(QString enemyHeroCode);
    void mascotMulliganDone();
    void mascotRewards(int wins);
    void mascotReadyUpWins(int wins);
    void mascotDraftFinished(int knownCards, float avgFire, float avgHA);
    void mascotGreeting();
    void mascotRedraftScreen(int screen);
    void mascotArenaRecord(int wins, int losses, bool lastWon);
    void mascotRunComplete();
    void mascotHeroes(int classOrder0, int classOrder1, int classOrder2);
    void mascotCards();
    void logReset();
    void completeArenaDeck();
    void setLocalLang();
    void replyFinished(QNetworkReply *reply);
    void missingOnWeb(QString code);
    void allCardsDownloaded();
    void init();
    void newGameResult(GameResult gameResult, LoadingScreenState loadingScreen);
    void newDeckCardDraft(QString code);
    void leaveArena();
    void readyFireWRMap(QMap<QString, float> *fireWRMap);
    void readyFireSamplesMap(QMap<QString, int> *fireSamplesMap);
};

#endif // MAINWINDOW_H
