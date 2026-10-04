#include "mainwindow.h"
#include "Utils/macwindow.h"
#include "Utils/macocr.h"
#include "utility.h"
#include "Widgets/cardwindow.h"
#include "versionchecker.h"
#include "Utils/hdimages.h"
#include "Utils/hdicons.h"
#include "Utils/pickrating.h"
#include <QtConcurrent/QtConcurrent>
#include <QtWidgets>


//Never shown: it owns the handlers and wires them; the user sees the mascot and the draft overlays
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent, Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint)
{
    initVariables();
    createNetworkManager();
    createDataDir();
    createLogFile();
    logPlatform();
    initCardsJson();
    downloadArenaVersion();
    downloadHearthArenaVersion();
    downloadExtraFiles();

    HDImages::create(this);
    HDIcons::prefetch();
    createCardDownloader();
    createWinratesDownloader();
    createDeckHandler();
    createArenaHandler();
    createDraftHandler();//-->DeckHandler -->ArenaHandler
    createGameWatcher();//-->A lot
    createLogLoader();//-->GameWatcher -->DraftHandler
    createCardWindow();
    createMascotWindow();//-->DraftHandler -->GameWatcher

    readSettings();
    checkFirstRunNewVersion();
    createVersionChecker();//Despues de createDataDir (removeHSDir) y checkFirstRunNewVersion() ya que reescribe el settings "runVersion"

    QTimer::singleShot(1000, this, SLOT(init()));

    new MacHoverTracker(this);
    new MacFullScreenOverlay(this);
}


MainWindow::~MainWindow()
{
    if(networkManager != nullptr)      delete networkManager;
    if(logLoader != nullptr)           delete logLoader;
    if(gameWatcher != nullptr)         delete gameWatcher;
    if(arenaHandler != nullptr)        delete arenaHandler;
    if(cardDownloader != nullptr)      delete cardDownloader;
    if(winratesDownloader != nullptr)  delete winratesDownloader;
    if(draftHandler != nullptr)        delete draftHandler;
    if(deckHandler != nullptr)         delete deckHandler;
    closeLogFile();
    QFontDatabase::removeAllApplicationFonts();
}


void MainWindow::initVariables()
{
    QSettings settings;

    atLogFile = nullptr;
    cardsJsonLoaded = arenaSetsLoaded = false;
    allCardsDownloadNeeded = !settings.value("allCardsDownloaded", false).toBool();
    Utility::setTrustHA(settings.value("trustHA", true).toBool());
    Utility::setArenaSets(settings.value("arenaSets", QStringList()).toStringList());

    logLoader = nullptr;
    gameWatcher = nullptr;
    arenaHandler = nullptr;
    cardDownloader = nullptr;
    winratesDownloader = nullptr;
    draftHandler = nullptr;
    deckHandler = nullptr;
}


void MainWindow::init()
{
    //Shown here, after MacFullScreenOverlay exists, so it can show over fullscreen Hearthstone too; not over the splash
    initDone = true;
    if(!splashOpen)     mascotWindow->show();
}


void MainWindow::logPlatform()
{
#ifdef QT_DEBUG
    pDebug("MODE DEBUG");
#endif


    pDebug("Platform: Mac");


    pDebug("Path Arena Dude Dir: " + Utility::dataPath());
}


void MainWindow::resetDeckDontRead()
{
    resetDeck(true);
}


void MainWindow::resetDeck(bool deckRead)
{
    gameWatcher->setDeckRead(deckRead);
    deckHandler->reset();
}


QString MainWindow::getHSLanguage()
{
    QString lang = "";

    if(logLoader != nullptr)
    {
        QDir dir(QFileInfo(logLoader->getLogConfigPath()).absolutePath() + "/Cache/UberText");
        dir.setFilter(QDir::Files);
        dir.setSorting(QDir::Time);


        switch (dir.count())
        {
        case 0:
            lang = "enUS";
            break;
        case 1:
            for(const QString &file: (const QStringList)dir.entryList())
            {
                lang = file.mid(5,4);
            }
            break;
        default:
            QStringList files = dir.entryList();
            lang = files.takeFirst().mid(5,4);

            //Remove old languages files
            for(const QString &file: qAsConst(files))
            {
                dir.remove(file);
                pDebug(file + " removed.");
            }

            //Remove old image cards
//            QDir dirHSCards(Utility::hscardsPath());
//            dirHSCards.setFilter(QDir::Files);
//            QStringList filters("*_*.png");
//            dirHSCards.setNameFilters(filters);

//            for(const QString &file: dirHSCards.entryList())
//            {
//                dirHSCards.remove(file);
//                pDebug(file + " removed.");
//            }

            break;
        }
    }


    if(lang != "enGB" && lang != "enUS" && lang != "esES" && lang != "esMX" &&
            lang != "deDE" && lang != "frFR" && lang != "itIT" &&
            lang != "plPL" && lang != "ptBR" && lang != "ruRU" &&
            lang != "koKR" && lang != "zhCN" && lang != "zhTW" &&
            lang != "jaJP" && lang != "thTH")
    {
        pDebug("Language: " + lang + " not supported. Using enUS.");
        lang = "enUS";
    }
    else if(lang == "enGB")
    {
        pDebug("Language: " + lang + ". Using enUS.");
        lang = "enUS";
    }
    else
    {
        pDebug("Language: " + lang + ".");
    }

    cardDownloader->setLang(lang);
    // lang = "enUS";//Test secrets
    return lang;
}


void MainWindow::createCardsJsonMap(QByteArray &jsonData)
{
    pDebug("Create Json Map.");

    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData);
    const QJsonArray jsonArray = jsonDoc.array();
    for(const QJsonValue &jsonCard: jsonArray)
    {
        QJsonObject jsonCardObject = jsonCard.toObject();
        cardsJson[jsonCardObject.value("id").toString()] = jsonCardObject;
    }

    cardsJsonLoaded = true;
    if(draftHandler != nullptr) draftHandler->buildHeroCodesList();
    checkArenaCards();
}


void MainWindow::replyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    QString fullUrl = reply->url().toString();
    QString endUrl = fullUrl.split("/").last();

    if(reply->error() != QNetworkReply::NoError)
    {
        pDebug(reply->url().toString() + " --> Failed. Retrying...");
        networkManager->get(QNetworkRequest(reply->url()));
    }
    else
    {
        //Cards version - Github
        if(endUrl == "cardsVersion.json")
        {
            int cardsVersion = QJsonDocument::fromJson(reply->readAll()).object().value("cardsVersion").toInt();
            downloadCardsJson(cardsVersion);
        }
        //Cards json
        else if(endUrl == "cards.json")
        {
            //Old redirection
            if(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 302)
            {
                checkCardsJsonVersion(reply->rawHeader("Location"));
            }
            //Cards json - Github
            else//CARDS_URL + QString("/cards.json")
            {
                pDebug("Extra: Json Cards --> Download Success.");
                QByteArray jsonData = reply->readAll();
                QString cardsJsonLocal = Utility::extraPath() + "/cards.json";
                Utility::dumpOnFile(jsonData, cardsJsonLocal);

                //Histograms/arena cards could have been built with the old cards.json (new sets missing).
                removeHistograms();
                Utility::createDir(Utility::histogramsPath());
                allCardsDownloadNeeded = true;
                Utility::setCardsJsonUpToDate(true);
                createCardsJsonMap(jsonData);
                initWRCards();
            }
        }
        //Arena version
        else if(endUrl == "arenaVersion.json")
        {
            checkArenaVersionJson(QJsonDocument::fromJson(reply->readAll()).object());
        }
        //HearthArena version
        else if(endUrl == "haVersion.json")
        {
            int haVersion = QJsonDocument::fromJson(reply->readAll()).object().value("haVersion").toInt();
            downloadHearthArenaJson(haVersion);
        }
        //HearthArena json
        else if(endUrl == "hearthArena.json")
        {
            pDebug("Extra: Json HearthArena --> Download Success.");
            QByteArray jsonData = reply->readAll();
            Utility::dumpOnFile(jsonData, Utility::extraPath() + "/hearthArena.json");
        }
        //Extra files
        else
        {
            pDebug("Extra: " + endUrl + " --> Download Success.");
            QByteArray data = reply->readAll();
            QString targetPath = Utility::extraPath() + "/" + endUrl;
            Utility::dumpOnFile(data, targetPath);
        }
    }
}


//Old redirection
void MainWindow::checkCardsJsonVersion(QString cardsJsonVersion)
{
    QSettings settings;
    QString storedCardsJsonVersion = settings.value("cardsJsonVersion", "").toString();
    QFile cardsJsonFile(Utility::extraPath() + "/cards.json");
    pDebug("Extra: Json Cards --> Latest version: " + cardsJsonVersion);
    pDebug("Extra: Json Cards --> Stored version: " + storedCardsJsonVersion);

    //Need download
    if(cardsJsonVersion != storedCardsJsonVersion || !cardsJsonFile.exists())
    {
        pDebug("Extra: Json Cards --> Download from: " + cardsJsonVersion);
        networkManager->get(QNetworkRequest(QUrl(cardsJsonVersion)));
    }
    //No download
    else
    {
        pDebug("Extra: Json Cards --> Use local cards.json");
        initWRCards();
    }
}


void MainWindow::setLocalLang()
{
    QString lang = getHSLanguage();
    Utility::setLocalLang(lang);
}


void MainWindow::initCardsJson()
{
    Utility::setCardsJson(&cardsJson);
    downloadCardsJsonVersion();

    //Load local cards.json (Incluso aunque haya una version nueva para bajar)
    QFile cardsJsonFile(Utility::extraPath() + "/cards.json");
    if(cardsJsonFile.exists())
    {
        if(!cardsJsonFile.open(QIODevice::ReadOnly))
        {
            pDebug("ERROR: Failed to open cards.json");
            return;
        }
        QByteArray jsonData = cardsJsonFile.readAll();
        cardsJsonFile.close();
        createCardsJsonMap(jsonData);
    }
}


void MainWindow::initHeroesWinrate()
{
    winratesDownloader->initHeroesWinrate();
}


void MainWindow::readyFireWRMap(QMap<QString, float> *fireWRMap)
{
    draftHandler->setFireWRMap(fireWRMap);
}


void MainWindow::readyFireSamplesMap(QMap<QString, int> *fireSamplesMap)
{
    draftHandler->setFireSamplesMap(fireSamplesMap);
}


void MainWindow::initWRCards()
{
    winratesDownloader->initWRCards();
}


void MainWindow::downloadArenaVersion()
{
    networkManager->get(QNetworkRequest(QUrl(ARENA_URL + QString("/arenaVersion.json"))));
}


void MainWindow::checkArenaVersionJson(const QJsonObject &jsonObject)
{
    int version = jsonObject.value("arenaVersion").toInt();
    bool needProcess = false;
    QSettings settings;
    int storedVersion = settings.value("arenaVersion", 0).toInt();
    if(version != storedVersion)    needProcess = true;
    if(settings.value("arenaSets", QStringList()).toStringList().isEmpty()) needProcess = true;

    pDebug("Extra: Json Arena github: Local(" + QString::number(storedVersion) + ") - "
                        "Web(" + QString::number(version) + ")" + (!needProcess?" up-to-date":""));

    if(needProcess)
    {
        bool multiclassArena = jsonObject.value("multiclassArena").toBool(false);
        if(draftHandler != nullptr) draftHandler->setMulticlassArena(multiclassArena);
        settings.setValue("multiclassArena", multiclassArena);
        pDebug("CheckArenaVersion: multiclassArena: " + QString(multiclassArena?"true":"false"));

        bool redownloadCards = jsonObject.value("resetCards").toInt() > storedVersion;
        pDebug("CheckArenaVersion: redownloadCards: " + QString(redownloadCards?"true":"false"));
        if(redownloadCards)
        {
            removeHSCards(true);
            Utility::createDir(Utility::hscardsPath());
        }

        //Arena Sets
        QStringList arenaSets;
        const QJsonArray jsonArray = jsonObject.value("arenaSets").toArray();
        for(const QJsonValue &jsonValue: jsonArray) arenaSets.append(jsonValue.toString());

        if(settings.value("arenaSets", QStringList()).toStringList() != arenaSets)
        {
            settings.setValue("arenaSets", arenaSets);
            Utility::setArenaSets(arenaSets);
            pDebug("CheckArenaVersion: New arena sets: " + arenaSets.join(" "));

            //New rotation date
            settings.setValue("rotationDate", QDate::currentDate());
        }
        else
        {
            pDebug("CheckArenaVersion: Unchanged arena sets: " +
                        settings.value("arenaSets", QStringList()).toStringList().join(" "));
        }

        //Remove histograms
        removeHistograms();
        Utility::createDir(Utility::histogramsPath());

        allCardsDownloadNeeded = true;
        settings.setValue("arenaVersion", version);

        //TrustHA
        bool trustHA = jsonObject.value("trustHA").toBool(false);
        Utility::setTrustHA(trustHA);
        settings.setValue("trustHA", trustHA);
        pDebug("CheckArenaVersion: trustHA: " + QString(trustHA?"true":"false"));
    }
    else
    {
        QStringList arenaSets = settings.value("arenaSets", QStringList()).toStringList();
        bool trustHA = settings.value("trustHA", true).toBool();
        pDebug(QStringLiteral("CheckArenaVersion: Unchanged arena sets: %1").arg(arenaSets.join(" ")));
        pDebug(QStringLiteral("CheckArenaVersion: Unchanged trustHA: %1").arg(trustHA?"true":"false"));
    }

    arenaSetsLoaded = true;
    checkArenaCards();
}


void MainWindow::createNetworkManager()
{
    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(replyFinished(QNetworkReply*)));
}


void MainWindow::createVersionChecker()
{
    VersionChecker *versionChecker = new VersionChecker(this);
    connect(versionChecker, SIGNAL(pDebug(QString,DebugLevel,QString)),
            this, SLOT(pDebug(QString,DebugLevel,QString)));
}


//Durante un redraft repasamos los scores de todo el deck despues de cada pick, (despues de incluirlo en el deck)
void MainWindow::newDeckCardDraft(QString code)
{
    deckHandler->newDeckCardDraft(code);
    draftHandler->setDeckScores();
}


void MainWindow::createDraftHandler()
{
    draftHandler = new DraftHandler(this, deckHandler);
    connect(winratesDownloader, SIGNAL(readyHeroesWinrate()),
            draftHandler, SLOT(updateHeroScores()));
    connect(draftHandler, SIGNAL(checkCardImage(QString)),
            this, SLOT(checkCardImage(QString)));

    connect(draftHandler, SIGNAL(newDeckCard(QString)),
            this, SLOT(newDeckCardDraft(QString)));
    connect(draftHandler, SIGNAL(draftEnded(QString)),
            deckHandler, SLOT(saveDraftDeck(QString)));
    connect(draftHandler, SIGNAL(saveDraftDeck(QString)),
            deckHandler, SLOT(saveDraftDeck(QString)));
    connect(draftHandler, SIGNAL(deleteDraftDeck(QString)),
            deckHandler, SLOT(deleteDraftDeck(QString)));

    connect(draftHandler, SIGNAL(draftEnded(QString)),
            arenaHandler, SLOT(newArena(QString)));
    connect(draftHandler, SIGNAL(scoreAvg(int,float,QString)),
            arenaHandler, SLOT(setCurrentAvgScore(int,float,QString)));

    connect(draftHandler, SIGNAL(pDebug(QString,DebugLevel,QString)),
            this, SLOT(pDebug(QString,DebugLevel,QString)));


    initHeroesWinrate();
    if(cardsJsonLoaded) draftHandler->buildHeroCodesList();

    QSettings settings;
    bool multiclassArena = settings.value("multiclassArena", false).toBool();
    draftHandler->setMulticlassArena(multiclassArena);
    pDebug("multiclassArena = " + QString(multiclassArena?"true":"false"));
}


void MainWindow::createArenaHandler()
{
    arenaHandler = new ArenaHandler(this);
    connect(arenaHandler, SIGNAL(pDebug(QString,DebugLevel,QString)),
            this, SLOT(pDebug(QString,DebugLevel,QString)));
    arenaHandler->loadStatsJsonFile();
}


void MainWindow::createDeckHandler()
{
    deckHandler = new DeckHandler(this);
    connect(deckHandler, SIGNAL(checkCardImage(QString)),
            this, SLOT(checkCardImage(QString)));
    connect(deckHandler, SIGNAL(pDebug(QString,DebugLevel,QString)),
            this, SLOT(pDebug(QString,DebugLevel,QString)));
}


//The card preview of the overlays and the mascot's card rows
void MainWindow::createCardWindow()
{
    cardWindow = new CardWindow(this);
    connect(draftHandler, SIGNAL(overlayCardEntered(QString,QRect,int,int,bool)),
            cardWindow, SLOT(loadCard(QString,QRect,int,int,bool)));
    connect(draftHandler, SIGNAL(overlayCardLeave()),
            cardWindow, SLOT(hide()));
    connect(draftHandler, SIGNAL(draftStarted()),
            cardWindow, SLOT(hide()));
}


void MainWindow::createCardDownloader()
{
    cardDownloader = new HSCardDownloader(this);
    connect(cardDownloader, SIGNAL(downloaded(QString)),
            this, SLOT(redrawDownloadedCardImage(QString)));
    connect(cardDownloader, SIGNAL(missingOnWeb(QString)),
            this, SLOT(missingOnWeb(QString)));
    connect(cardDownloader, SIGNAL(allCardsDownloaded()),
            this, SLOT(allCardsDownloaded()));
    connect(cardDownloader, SIGNAL(pDebug(QString,DebugLevel,QString)),
            this, SLOT(pDebug(QString,DebugLevel,QString)));
}


void MainWindow::createWinratesDownloader()
{
    winratesDownloader = new WinratesDownloader(this);
    connect(winratesDownloader, SIGNAL(readyFireWRMap(QMap<QString,float>*)),
            this, SLOT(readyFireWRMap(QMap<QString,float>*)));
    connect(winratesDownloader, SIGNAL(readyFireSamplesMap(QMap<QString,int>*)),
            this, SLOT(readyFireSamplesMap(QMap<QString,int>*)));
    connect(winratesDownloader, SIGNAL(pDebug(QString,DebugLevel,QString)),
            this, SLOT(pDebug(QString,DebugLevel,QString)));
}


//Al salir de arena queremos guardar el deck de arena antes de que se reinicie (por deckHandler)
void MainWindow::leaveArena()
{
    draftHandler->leaveArena();
    deckHandler->leaveArena();
}


void MainWindow::createGameWatcher()
{
    gameWatcher = new GameWatcher(this);

    connect(gameWatcher, SIGNAL(newArena(QString)),
            this, SLOT(resetDeckDontRead()));
    connect(gameWatcher, SIGNAL(needResetDeck()),
            this, SLOT(resetDeck()));
    connect(gameWatcher, SIGNAL(arenaDeckRead()),
            this, SLOT(completeArenaDeck()));
    connect(gameWatcher, SIGNAL(leaveArena()),
            this, SLOT(leaveArena()));
    connect(gameWatcher, SIGNAL(pDebug(QString,qint64,DebugLevel,QString)),
            this, SLOT(pDebug(QString,qint64,DebugLevel,QString)));

    connect(gameWatcher, SIGNAL(newGameResult(GameResult,LoadingScreenState)),
            this, SLOT(newGameResult(GameResult,LoadingScreenState)));
    connect(gameWatcher, SIGNAL(newDeckCard(QString)),
            deckHandler, SLOT(newDeckCardAsset(QString)));
    connect(gameWatcher, &GameWatcher::deckSnapshotRead, deckHandler, &DeckHandler::syncDeckSnapshot);
    connect(gameWatcher, SIGNAL(enterArena()),
            deckHandler, SLOT(enterArena()));

    connect(gameWatcher, SIGNAL(newArena(QString)),
            draftHandler, SLOT(beginDraft(QString)));
    connect(gameWatcher, SIGNAL(continueDraft()),
            draftHandler, SLOT(continueDraft()));
    connect(gameWatcher, SIGNAL(redraft()),
            draftHandler, SLOT(redraft()));
    connect(gameWatcher, SIGNAL(checkRedraft()),
            draftHandler, SLOT(checkRedraft()));
    connect(gameWatcher, SIGNAL(arenaChoosingHeroe()),
            draftHandler, SLOT(beginHeroDraft()));
    connect(gameWatcher, SIGNAL(heroDraftDeck(QString)),
            draftHandler, SLOT(heroDraftDeck(QString)));
    connect(gameWatcher, SIGNAL(activeDraftDeck()),
            draftHandler, SLOT(activeDraftDeck()));
    connect(gameWatcher, SIGNAL(startGame()),
            draftHandler, SLOT(stopDraft()));
    connect(gameWatcher, SIGNAL(pickCard(QString)),
            draftHandler, SLOT(pickCard(QString)));
}


void MainWindow::createLogLoader()
{
    logLoader = new LogLoader(this);
    connect(logLoader, SIGNAL(logReset()),
            this, SLOT(logReset()));
    connect(logLoader, SIGNAL(newLogLineRead(LogComponent,QString,qint64,qint64)),
            gameWatcher, SLOT(processLogLine(LogComponent,QString,qint64,qint64)));
    connect(logLoader, SIGNAL(logConfigSet()),
            this, SLOT(setLocalLang()));
    connect(logLoader, SIGNAL(pDebug(QString,DebugLevel,QString)),
            this, SLOT(pDebug(QString,DebugLevel,QString)));

    //Connect de draftHandler
    connect(draftHandler, SIGNAL(draftEnded(QString)),
            logLoader, SLOT(setUpdateTimeMax()));
    connect(draftHandler, SIGNAL(draftStarted()),
            logLoader, SLOT(setUpdateTimeMin()));

    if(!logLoader->init())  QTimer::singleShot(1, this, SLOT(closeApp()));
}


void MainWindow::newGameResult(GameResult gameResult, LoadingScreenState loadingScreen)
{
    arenaHandler->newGameResult(gameResult, loadingScreen);
}


void MainWindow::closeApp()
{
    draftHandler->stopDraft();
    draftHandler->closeFindScreenRects();
    winratesDownloader->waitFinishThreads();
    close();
    //On macOS closing the window used to leave the app running in the Dock, with no way to show it again
    qApp->quit();
}


//One of the lines at random, so the mascot doesn't repeat itself
static QString mascotPick(const QStringList &lines)
{
    return lines[QRandomGenerator::global()->bounded(lines.count())];
}


void MainWindow::createMascotWindow()
{
    mascotWindow = new MascotWindow();
    connect(mascotWindow, SIGNAL(quitRequested()),
            this, SLOT(closeApp()));
    connect(mascotWindow, &MascotWindow::discordRequested, this, []() {
        QDesktopServices::openUrl(QUrl(MASCOT_DISCORD_URL));
    });
    connect(mascotWindow, &MascotWindow::supportRequested, this, []() {
        QDesktopServices::openUrl(QUrl(MASCOT_SUPPORT_URL));
    });
    //The log goes with the report: shown in Finder, ready to drop into the Discord
    connect(mascotWindow, &MascotWindow::reportRequested, this, []() {
        const QString logPath = Utility::dataPath() + "/ArenaDudeLog.txt";
        QProcess::startDetached("open", {"-R", logPath});
        QDesktopServices::openUrl(QUrl(MASCOT_DISCORD_URL));
    });
    connect(mascotWindow, SIGNAL(cardEntered(QString,QRect,int,int)),
            cardWindow, SLOT(loadCard(QString,QRect,int,int)));
    connect(mascotWindow, SIGNAL(cardLeave()),
            cardWindow, SLOT(hide()));
    connect(draftHandler, SIGNAL(draftStatusChanged(QString)),
            this, SLOT(mascotDraftStatus(QString)));
    connect(gameWatcher, SIGNAL(startGame()),
            this, SLOT(mascotStartGame()));
    connect(gameWatcher, SIGNAL(endGame(bool,bool)),
            this, SLOT(mascotEndGame(bool,bool)));
    connect(gameWatcher, SIGNAL(enemySecretPlayed()),
            this, SLOT(mascotEnemySecret()));
    connect(draftHandler, SIGNAL(redraftScreenChanged(int)),
            this, SLOT(mascotRedraftScreen(int)));
    connect(arenaHandler, SIGNAL(arenaRecordChanged(int,int,bool)),
            this, SLOT(mascotArenaRecord(int,int,bool)));
    connect(gameWatcher, &GameWatcher::arenaRetired, this, [this]() { mascotRetired = true; });
    //No run between its rewards screen and the next draft (also for the logs replayed at startup)
    connect(gameWatcher, &GameWatcher::inRewards, this, [this]() { mascotNoRun = true; });
    connect(gameWatcher, &GameWatcher::arenaChoosingHeroe, this, [this]() { mascotNoRun = false; });
    connect(gameWatcher, &GameWatcher::newArena, this, [this]() { mascotNoRun = false; });
    connect(gameWatcher, SIGNAL(inRewards()),
            this, SLOT(mascotRunComplete()));
    connect(draftHandler, &DraftHandler::rewardsWinsRead, this, &MainWindow::mascotRewards);
    connect(draftHandler, &DraftHandler::readyUpWinsRead, this, &MainWindow::mascotReadyUpWins);
    connect(draftHandler, SIGNAL(heroesScored(int,int,int)),
            this, SLOT(mascotHeroes(int,int,int)));
    connect(draftHandler, SIGNAL(cardsScored()),
            this, SLOT(mascotCards()));
    connect(draftHandler, SIGNAL(draftFinished(int,float,float)),
            this, SLOT(mascotDraftFinished(int,float,float)));
    connect(logLoader, &LogLoader::logsCaughtUp, arenaHandler, &ArenaHandler::setLogsCaughtUp);
    connect(mascotWindow, &MascotWindow::said, this, [this](const QString &text) { pDebug("Mascot: " + text); });
    connect(logLoader, &LogLoader::logsCaughtUp, this, [this]() {
        mascotLive = true;
        mascotGreeting();
    });
}


//The draft status in the mascot's words, with a matching face
void MainWindow::mascotDraftStatus(QString text)
{
    if(!mascotLive)     return;     //The picks replayed at startup; mascotGreeting says where we are
    if(text.isEmpty())
    {
        //Only its own bubble: the discard screen clears the status right when the mascot shows the cards to remove
        if(!mascotSaysStatus)   return;
        mascotSaysStatus = false;
        mascotWindow->setMood(MascotWindow::Idle);
        mascotWindow->say("");
        return;
    }

    //The pick advice stays until a new pick is scanned or something goes wrong: the other statuses (reading a
    //legendary group's preview or the deck list, notices) come while the player still looks at the pick
    if(mascotSaysAdvice)
    {
        const bool newPickOrProblem = (text.startsWith("Scanning") && !text.startsWith("Scanning the deck list")) ||
                                      text.startsWith("Looking for the arena") ||
                                      text.startsWith("Can't see") || text.contains("Game Mode");
        if(!newPickOrProblem)   return;
        mascotSaysAdvice = false;
    }

    //The same status again (e.g. a retry) keeps the line: the random variants would flicker
    if(mascotSaysStatus && text == mascotLastStatus)    return;
    //Two loops disagreeing (a status coming back within seconds of the current one) would flip the line every
    //second: the current one stays
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 flipTime = 5000;
    if(mascotSaysStatus && now - mascotStatusShownAt.value(mascotLastStatus, 0) < flipTime &&
            now - mascotStatusShownAt.value(text, 0) < flipTime)
    {
        mascotStatusShownAt[mascotLastStatus] = now;
        return;
    }
    mascotStatusShownAt[text] = now;
    mascotLastStatus = text;

    MascotWindow::Mood mood = text.endsWith("...") ? MascotWindow::Thinking : MascotWindow::Idle;
    QString line = text;
    if(text.contains("Game Mode"))
    {
        mood = MascotWindow::Smug;
        line = "Everything's under control. But macOS Game Mode slows me down: turn it off with the gamepad icon in the menu bar.";
    }
    else if(text.startsWith("Can't see the arena screen"))
    {
        mood = MascotWindow::Blind;
        line = "I can't see the arena! Give me Screen Recording permission and I'm all yours.";
        mascotWindow->setMood(mood);
        mascotWindow->say(line, 0, "Open Settings", []() {
            QDesktopServices::openUrl(QUrl(SCREEN_RECORDING_SETTINGS_URL));
        });
        mascotSaysStatus = true;
        return;
    }
    else if(text.startsWith("Looking for the discard screen"))
    {
        //On the discard or Ready Up screen the mascot already said something better
        if(mascotRedraftScreenShown != RedraftScreenOther)  return;
        line = "Waiting for the discard screen...";
    }
    else if(text.startsWith("Looking for the arena screen"))    line = "Where's the arena? Show me the draft.";
    else if(text.startsWith("Scanning heroes"))                 line = mascotPick({"Picking a hero? Let me see...",
                                                                                  "Heroes, huh. Let me take a look..."});
    else if(text.startsWith("Scanning"))                        line = mascotPick({"Hmm... let me look at these cards...",
                                                                                  "Let me take a look...",
                                                                                  "Checking the numbers...",
                                                                                  "Hold on, genius at work..."});
    else if(text.startsWith("Analyzing bundle"))                line = "Let me peek at this group...";
    else if(text.startsWith("Bundle read"))                     line = "Got the group's cards. Choose whenever you're ready.";
    else if(text.startsWith("Can't read this bundle"))          line = "Can't read this group. I'll catch its cards after the pick.";
    else if(text.startsWith("Downloading card images"))         line = "Grabbing card pics" + text.mid(QString("Downloading card images").length());
    mascotWindow->setMood(mood);
    mascotWindow->say(line);
    mascotSaysStatus = true;
}


//After a draft or a redraft, on the Ready Up screen
QStringList MainWindow::mascotGoodLuckLines()
{
    return {"Go get 'em!",
            "Good luck. Not that you need it with my picks.",
            "Queue up. I've got the popcorn ready.",
            "Go win. I'll be judging every misplay.",
            "You've got this. And you've got me."};
}


void MainWindow::setSplashOpen()
{
    splashOpen = true;
}


//The first run downloads the card images under the splash: the mascot comes after it
void MainWindow::splashClosed()
{
    splashOpen = false;
    if(initDone)    mascotWindow->show();
}


//Once the logs are caught up: the tracker may start in the middle of a draft or a run
void MainWindow::mascotGreeting()
{
    bool hearthstoneRunning = true;
    hearthstoneRunning = !MacOcr::hearthstoneWindowRect().isNull();
    bool screenRecording = true;
    screenRecording = MacWindow::hasScreenRecording();
    if(!screenRecording && MacWindow::isNative())   MacWindow::requestScreenRecording();     //Not in an offscreen test run
    //Without it the mascot sees no draft: asked first. Stays until something else is said.
    if(!screenRecording)
    {
        mascotWindow->setMood(MascotWindow::Blind);
        //The log settings written now aren't checked again at the next start: Hearthstone's restart is asked here too
        QString line = "I can't see your screen yet. Allow me in System Settings > Privacy & Security > "
                       "Screen Recording, then restart me.";
        if(logLoader->isHearthstoneRestartNeeded() && hearthstoneRunning)
            line += " Restart Hearthstone too, so I can read its logs.";
        //Opens System Settings on the Screen Recording list (macOS offers "Quit & Reopen" once it's turned on)
        mascotWindow->say(line, 0, "Open Settings", []() {
            QDesktopServices::openUrl(QUrl(SCREEN_RECORDING_SETTINGS_URL));
        });
    }
    //First run with Hearthstone already open: it only logs after a restart. Stays until something else is said.
    else if(logLoader->isHearthstoneRestartNeeded() && hearthstoneRunning)
    {
        mascotWindow->setMood(MascotWindow::Sweat);
        mascotWindow->say("First time here? Restart Hearthstone so I can read its logs. I'll wait.");
    }
    else if(draftHandler->isRedrafting())
    {
        mascotWindow->setMood(MascotWindow::Smile);
        mascotWindow->say(mascotPick({"Redraft time! Let's patch this deck up.",
                                      "A redraft? Good, I had some notes on this deck anyway."}), 10000);
    }
    else if(draftHandler->isDrafting() && !draftHandler->isEmptyDeck())
    {
        mascotWindow->setMood(MascotWindow::Sweat);
        mascotWindow->say("You started without me? No worries, I missed a few picks but I'll help with the rest.", 10000);
    }
    else if(!draftHandler->isDrafting() && getLoadingScreen() == arena && mascotNoRun)
    {
        mascotWindow->setMood(MascotWindow::Smile);
        mascotWindow->say(mascotPick({"Fresh start. Let's draft a winner.",
                                      "New run? I've been waiting for this. Let's go."}), 8000);
    }
    else if(!draftHandler->isDrafting() && getLoadingScreen() == arena)
    {
        mascotWindow->setMood(MascotWindow::Smile);
        mascotWindow->say(mascotPick({"Oh, you're back. I kept the popcorn warm.",
                                      "Welcome back. Let's pick up where we left off.",
                                      "There you are. The deck's been waiting."}), 8000);
    }
    //The main menu, or Hearthstone not started yet: the line waits until the mascot shows with Hearthstone
    else if(!draftHandler->isDrafting())
    {
        mascotWindow->setMood(MascotWindow::Smile);
        mascotWindow->say(mascotPick({"Hey! Open the arena, I'll help you draft something great.",
                                      "Arena time? Relax, I've got the brains here.",
                                      "Ready when you are. Open the arena, let's win something."}), 10000);
    }
}


//A new deck is done (the Ready Up screen): its average score and good luck
void MainWindow::mascotDraftFinished(int knownCards, float avgFire, float avgHA)
{
    if(!mascotLive)     return;
    mascotSaysAdvice = false;
    mascotSaysStatus = false;

    QString line;
    MascotWindow::Mood mood = MascotWindow::Smug;
    if(avgFire > 0 || avgHA > 0)
    {
        //Thresholds are a first guess of what a good arena deck averages
        const bool byFire = avgFire > 0;
        const float avg = byFire ? avgFire : avgHA;
        line = byFire ? QStringLiteral("Deck avg: %1% winrate. ").arg(avgFire, 0, 'f', 1)
                      : QStringLiteral("Deck avg: %1 on HearthArena. ").arg(qRound(avgHA));
        if(avg >= (byFire ? 54 : 80))
        {
            mood = MascotWindow::Stars;
            line += mascotPick({"A monster. We did good. ", "A monster. I've outdone myself. "});
        }
        else if(avg >= (byFire ? 52.5f : 65))
        {
            mood = MascotWindow::Grin;
            line += mascotPick({"Solid deck, nice drafting. ", "Solid. My work, obviously. "});
        }
        else if(avg >= (byFire ? 51 : 55))  line += mascotPick({"Decent. We can win with this. ", "Decent. The cards just didn't deserve me. "});
        else
        {
            mood = MascotWindow::Sweat;
            line += mascotPick({"Rough one, but we'll make it work. ", "Rough. The arena offered garbage, I made it edible. "});
        }
        if(knownCards < 30)     line += QStringLiteral("(Only the %1 cards I saw.) ").arg(knownCards);
    }
    line += mascotPick(mascotGoodLuckLines());
    mascotWindow->setMood(mood);
    mascotWindow->say(line, 15000);
}


//The discard screen of a redraft, or the Ready Up screen after it
void MainWindow::mascotRedraftScreen(int screen)
{
    mascotSaysAdvice = false;
    mascotRedraftScreenShown = screen;
    mascotSaysStatus = false;
    if(screen == RedraftScreenDiscard)
    {
        QList<MascotWindow::Section> sections;
        const QList<RedraftSuggestion> suggestions = draftHandler->getRedraftRemoveSuggestions();
        for(const RedraftSuggestion &suggestion: suggestions)
        {
            MascotWindow::Section section{suggestion.source, {}};
            for(const RedraftSuggestionCard &card: suggestion.cards)
                section.rows << MascotWindow::Row{card.name, card.score, card.code, mascotRarityColor(card.code)};
            sections << section;
        }
        mascotWindow->setMood(MascotWindow::Point);
        if(sections.isEmpty())  mascotWindow->say("Cut the weakest ones. I have no scores for this deck, sorry.");
        else                    mascotWindow->saySections(mascotPick({"Cut these, if you ask me:",
                                                                      "These go. Trust me:",
                                                                      "Cut these, and don't get sentimental:"}), sections);
    }
    else if(screen == RedraftScreenReadyUp)
    {
        mascotWindow->setMood(MascotWindow::Smug);
        mascotWindow->say("Deck's ready. " + mascotPick(mascotGoodLuckLines()), 10000);
    }
    else
    {
        mascotWindow->setMood(MascotWindow::Idle);
        mascotWindow->say("");
    }
}


//Praise by wins, sympathy by losses, and once per good run a nudge to support the development
void MainWindow::mascotArenaRecord(int wins, int losses, bool lastWon)
{
    if(!mascotLive)     return;
    mascotSaysStatus = false;
    if(wins + losses <= 1)  mascotSupportAsked = false;     //A new run
    mascotLastWon = lastWon;
    mascotLastLosses = losses;

    //The game's line came at its end (mascotEndGame): here only the run's milestones
    QString line;
    MascotWindow::Mood mood = MascotWindow::Happy;
    if(lastWon)
    {
        static const QMap<int, QString> winLines = {
            {5, "Five wins. Not bad for someone who listens to me."},
            {6, "Six! Now we're cooking."},
            {7, "SEVEN WINS. Told you this deck was a beast."},
            {8, "Eight. We're basically geniuses. Mostly me."},
            {9, "Nine wins! The opponents are starting to cry."},
            {10, "Ten! Somebody call Blizzard, we broke the arena."},
            {11, "Eleven. One more. Don't choke. No pressure. Okay, some pressure."},
            {12, "TWELVE WINS! I drafted it, you just clicked. We're legends."}
        };
        line = winLines.value(wins);
        if(wins >= 7)   mood = MascotWindow::Stars;
        else if(wins >= 5)  mood = MascotWindow::Grin;
    }
    else
    {
        mood = MascotWindow::Sweat;
        //The final wins are said on the rewards screen, from its chest: the tracker may have missed games
        if(losses >= 3)         line = mascotPick({"That's three. Let's go see the loot.",
                                                   "Three losses, run's over. Chin up, loot time."});
        else if(losses == 2)    line = mascotPick({"Two losses. Careful now, one more and we're done.",
                                                   "Two down. Deep breath, we've still got this."});
    }
    if(!line.isEmpty())
    {
        mascotWindow->setMood(mood);
        mascotWindow->say(line, 8000);
    }

    //After a win, back on the Ready Up screen, its medal tells the real wins: the support ask comes on a good run,
    //after the win line
    if(lastWon && !mascotSupportAsked)
        QTimer::singleShot(6000, this, [this]() { if(!mascotInGame)    draftHandler->readReadyUpWins(); });
}


//The support ask, once per run, right after a win that takes the run to MASCOT_SUPPORT_WINS or more
void MainWindow::mascotReadyUpWins(int wins)
{
    if(!mascotLive || mascotInGame || mascotSupportAsked || wins < MASCOT_SUPPORT_WINS)  return;
    mascotSupportAsked = true;
    mascotSaysStatus = false;
    mascotWindow->setMood(MascotWindow::Smile);
    mascotWindow->say(QStringLiteral("%1 wins and counting! Enjoying them? Support my development. Genius runs on popcorn.").arg(wins),
                      15000, "Support", []() {
        QDesktopServices::openUrl(QUrl(MASCOT_SUPPORT_URL));
    });
}


//The run is over (the rewards screen): its chest shows the final wins, read before saying them
void MainWindow::mascotRunComplete()
{
    mascotRewardsRetired = mascotRetired;
    mascotRetired = false;
    if(!mascotLive)     return;
    mascotSaysStatus = false;
    draftHandler->readRewardsWins();
}


//The final wins (-1: the chest couldn't be read, the tracker's record is used)
void MainWindow::mascotRewards(int wins)
{
    if(!mascotLive)     return;
    MascotWindow::Mood mood = MascotWindow::Sweat;
    QString line;
    if(mascotRewardsRetired)
    {
        mood = MascotWindow::Smile;
        line = mascotPick({"Retired? Fair call. Some decks just aren't meant to be. Next one's ours.",
                           "A tactical retreat. Let's draft a better one.",
                           "Retiring? Smart. I never liked that deck anyway."});
    }
    else if(wins < 0)
    {
        line = mascotLastWon ? "TWELVE WINS! I drafted it, you just clicked. We're legends."
                             : "Run's over. Good run anyway. Next draft will be even better.";
        mood = mascotLastWon ? MascotWindow::Stars : MascotWindow::Sweat;
    }
    else if(wins == 12)
    {
        mood = MascotWindow::Stars;
        line = "TWELVE WINS! I drafted it, you just clicked. We're legends.";
    }
    else if(wins >= 7)
    {
        mood = MascotWindow::Stars;
        line = QStringLiteral("%1 wins! ").arg(wins) + mascotPick({"That's a monster run. Told you that deck was good.",
                                                                     "What a run. We make a great team. Mostly me."});
    }
    else if(wins >= MASCOT_SUPPORT_WINS)
    {
        mood = MascotWindow::Grin;
        line = QStringLiteral("%1 wins. ").arg(wins) + mascotPick({"Solid run, enjoy the loot.",
                                                                     "Not bad at all. Next one goes even deeper."});
    }
    else
    {
        line = QStringLiteral("Run's over: %1 win%2. ").arg(wins).arg(wins == 1 ? "" : "s") +
               mascotPick({"Good run anyway. Next draft will be even better.",
                           "The arena wasn't kind today. We'll get it next time."});
    }
    mascotWindow->setMood(mood);
    mascotWindow->say(line, 10000);
}


//The hero with the best class winrate, said by how far ahead it is, and a Rescan in case the heroes were read wrong
void MainWindow::mascotHeroes(int classOrder0, int classOrder1, int classOrder2)
{
    static const QStringList classNames = {"Death Knight", "Demon Hunter", "Druid", "Hunter", "Mage", "Paladin",
                                           "Priest", "Rogue", "Shaman", "Warlock", "Warrior"};
    auto className = [](int classOrder) { return (classOrder >= 0 && classOrder < classNames.count())?classNames[classOrder]:QString("?"); };

    //Best and second best by winrate
    QList<QPair<float, int>> heroes;
    for(int classOrder: {classOrder0, classOrder1, classOrder2})    heroes << qMakePair(WinratesDownloader::getHeroScore(classOrder), classOrder);
    std::sort(heroes.begin(), heroes.end(), [](const QPair<float, int> &a, const QPair<float, int> &b) { return a.first > b.first; });

    QString line;
    if(heroes[0].first <= 0)    line = "No winrates for these heroes yet. Go with your gut.";
    else
    {
        const QString best = className(heroes[0].second), second = className(heroes[1].second);
        const QString winrate = QString::number(heroes[0].first, 'f', 1) + "%";
        const float lead = heroes[0].first - heroes[1].first;
        QStringList lines;
        if(lead >= 3)
        {
            lines = {QStringLiteral("%1 is a no-brainer here. %2 winrate.").arg(best, winrate),
                     QStringLiteral("%1. Don't even think about the other two. %2.").arg(best, winrate),
                     QStringLiteral("%1, obviously. %2 winrate, the rest is trash.").arg(best, winrate)};
        }
        else if(lead >= 1)
        {
            lines = {QStringLiteral("I'd take %1. %2 winrate, a notch above %3.").arg(best, winrate, second),
                     QStringLiteral("%1 has the edge: %2 winrate.").arg(best, winrate),
                     QStringLiteral("Go %1. %2, the others can't keep up.").arg(best, winrate)};
        }
        else
        {
            lines = {QStringLiteral("Coin flip between %1 and %2. I'd go %1, %3.").arg(best, second, winrate),
                     QStringLiteral("%1 or %2, basically the same. %1 by a hair: %3.").arg(best, second, winrate),
                     QStringLiteral("Tough one, even for me. %1 at %3, %2 right behind.").arg(best, second, winrate)};
        }
        line = lines[QRandomGenerator::global()->bounded(lines.count())];
    }

    mascotSaysAdvice = true;
    mascotSaysStatus = false;
    mascotWindow->setMood(MascotWindow::Point);
    mascotWindow->say(line + "\n\nAm I hallucinating? Try:", 0, "Rescan", [this]() {
        mascotWindow->setMood(MascotWindow::Thinking);
        mascotWindow->say(mascotPick({"Rescanning... let me take a better look.", "Rescanning... okay, even geniuses blink."}));
        mascotSaysAdvice = false;
        mascotSaysStatus = true;    //Replaced by the draft status or the heroes again
        draftHandler->rescan();
    });
}


//Card names in the mascot's bubble, in their rarity color (darker than in the game, to read on white)
QColor MainWindow::mascotRarityColor(const QString &code)
{
    switch(Utility::getRarityFromCode(code))
    {
        case RARE:      return QColor(0, 112, 221);
        case EPIC:      return QColor(163, 53, 238);
        case LEGENDARY: return QColor(230, 120, 0);
        case COMMON:
        case FREE:      return QColor(120, 120, 120);
        default:        return Qt::black;
    }
}


//The pick with the best score (Firestone, else HearthArena), said by how far ahead it is
void MainWindow::mascotCards()
{
    const PickScores pick = draftHandler->getPickScores();
    const bool byFire = pick.showFire && std::max({pick.fire[0], pick.fire[1], pick.fire[2]}) > 0;

    //Ordered by the rating of both sources (Firestone trusted by its games, HearthArena); the line quotes Firestone
    float ratings[3];
    for(int i=0; i<3; i++)
    {
        ratings[i] = PickRating::rating({pick.showFire ? pick.fire[i] : 0, pick.fireGames[i], pick.showHA ? pick.ha[i] : 0});
    }
    int order[3] = {0, 1, 2};
    std::sort(order, order+3, [&ratings](int a, int b) { return ratings[a] > ratings[b]; });
    const int bestIndex = order[0];
    const bool anyScore = std::max({pick.fire[0], pick.fire[1], pick.fire[2], pick.ha[0], pick.ha[1], pick.ha[2]}) > 0;

    QString line;
    if(!anyScore || !PickRating::isReady())  line = "No scores for these. Trust your gut, you've got this.";
    else
    {
        QString best = MascotWindow::colored(pick.names[bestIndex], mascotRarityColor(pick.codes[bestIndex]));
        QString second = MascotWindow::colored(pick.names[order[1]], mascotRarityColor(pick.codes[order[1]]));
        if(pick.legendaryGroup)
        {
            best += "'s group";
            second += "'s group";
        }
        const QString score = (byFire && pick.fire[bestIndex] > 0) ? QString::number(pick.fire[bestIndex], 'f', 1) + "% winrate" :
                                                                     QString::number(qRound(pick.ha[bestIndex])) + " on HearthArena";
        const float lead = ratings[bestIndex] - ratings[order[1]];
        const float big = 0.6f, small = 0.25f;
        QStringList lines;
        if(lead >= big)
        {
            lines = {QStringLiteral("%1 is a no-brainer. %2.").arg(best, score),
                     QStringLiteral("%1, easy. %2, the other two are filler.").arg(best, score),
                     QStringLiteral("Take %1 and don't look back. %2.").arg(best, score),
                     QStringLiteral("%1. %2. I'd bet my hat on it.").arg(best, score)};
        }
        else if(lead >= small)
        {
            lines = {QStringLiteral("I'd take %1. %2, a notch above %3.").arg(best, score, second),
                     QStringLiteral("%1 has the edge: %2.").arg(best, score),
                     QStringLiteral("Go %1. %2. Trust me on this one.").arg(best, score)};
        }
        else
        {
            lines = {QStringLiteral("Coin flip between %1 and %2. I'd go %1: %3.").arg(best, second, score),
                     QStringLiteral("%1 or %2, basically the same. %1 by a hair.").arg(best, second),
                     QStringLiteral("Tough one, even for me. %1 at %3, %2 right behind.").arg(best, second, score)};
        }
        line = lines[QRandomGenerator::global()->bounded(lines.count())];
        if(byFire && pick.fireGames[bestIndex] >= 0 && pick.fireGames[bestIndex] < 200)
            line += QStringLiteral(" Only %1 games though, grain of salt.").arg(pick.fireGames[bestIndex]);
        //Not the best Firestone winrate: HearthArena made the difference
        int bestFire = 0;
        for(int i=1; i<3; i++)  if(pick.fire[i] > pick.fire[bestFire])    bestFire = i;
        if(byFire && pick.showHA && bestFire != bestIndex && pick.ha[bestIndex] > pick.ha[bestFire])
            line += mascotPick({" HearthArena rates it way higher, and so do I.", " Winrates are close, HearthArena breaks the tie."});
    }

    mascotSaysAdvice = true;
    mascotSaysStatus = false;
    mascotWindow->setMood(MascotWindow::Point);
    mascotWindow->say(line + "\n\nAm I hallucinating? Try:", 0, "Rescan", [this]() {
        mascotWindow->setMood(MascotWindow::Thinking);
        mascotWindow->say(mascotPick({"Rescanning... let me take a better look.", "Rescanning... okay, even geniuses blink."}));
        mascotSaysAdvice = false;
        mascotSaysStatus = true;    //Replaced by the draft status or the cards again
        draftHandler->rescan();
    });
}


void MainWindow::mascotStartGame()
{
    if(!mascotLive)     return;
    mascotSaysAdvice = false;
    mascotSaysStatus = false;
    mascotInGame = true;
    mascotWindow->setMood(MascotWindow::Popcorn);
    mascotWindow->say(mascotPick({"Popcorn time. Show me what this deck can do.",
                                  "Good luck! I'll be right here with the popcorn.",
                                  "Game on. I'll be judging every misplay."}), 5000);
}


//Secrets are not read yet: the mascot only notices them, and the first time asks for support to learn it
void MainWindow::mascotEnemySecret()
{
    if(!mascotLive || !mascotInGame)    return;
    mascotSaysStatus = false;
    const int msec = 7000;
    mascotWindow->setMood(MascotWindow::Detective);
    if(mascotSecretsSeen++ == 0)
    {
        mascotWindow->say("A secret! I can see it... but I can't read secrets yet. Help me learn?", 12000, "Support", []() {
            QDesktopServices::openUrl(QUrl(MASCOT_SUPPORT_URL));
        });
    }
    else
    {
        static const QStringList lines = {
            "Another secret. My magnifier is ready, my brain isn't. Coming soon.",
            "Secret spotted. What is it? No idea. Yet.",
            "Hmm, a secret. Detective school costs popcorn money.",
            "Elementary! It's a secret. That's all I've got."
        };
        mascotWindow->say(lines[QRandomGenerator::global()->bounded(lines.count())], msec);
    }
    //Back to the popcorn when the line is over, unless the game or the mood moved on
    QTimer::singleShot(mascotSecretsSeen == 1 ? 12000 : msec, this, [this]() {
        if(mascotInGame && mascotWindow->currentMood() == MascotWindow::Detective)
            mascotWindow->setMood(MascotWindow::Popcorn);
    });
}


void MainWindow::mascotEndGame(bool playerWon, bool playerUnknown)
{
    if(!mascotLive)     return;
    mascotSaysStatus = false;
    mascotInGame = false;
    if(playerUnknown)
    {
        mascotWindow->setMood(MascotWindow::Idle);
        return;
    }
    //Right away; back in the arena menu mascotArenaRecord only adds the run's milestones
    mascotWindow->setMood(playerWon ? MascotWindow::Happy : MascotWindow::Sweat);
    mascotWindow->say(playerWon ? mascotPick({"GG! Told you that deck was good.",
                                              "Nice one! As I calculated.",
                                              "GG. Great game, great deck.",
                                              "GG. I'd say you played well, but I watched."})
                                : mascotPick({"Unlucky. RNG hates us today.",
                                              "Shake it off. Next one's ours.",
                                              "A loss. Happens to the best of us. Even me, apparently."}), 8000);
}


//The settings of the old config tab that still matter
void MainWindow::readSettings()
{
    QSettings settings;

    int tooltipScale = settings.value("tooltipScale", 10).toInt();
    if(tooltipScale < 10)   tooltipScale = 10;
    cardWindow->scale(tooltipScale);


    draftHandler->setShowDraftScoresOverlay(settings.value("showDraftScoresOverlay", true).toBool());
    draftHandler->setShowMyWR(settings.value("showMyWR", true).toBool());
    draftHandler->setDraftMethod(settings.value("draftMethodHA", true).toBool(), settings.value("draftMethodFire", true).toBool());
}


void MainWindow::pDebug(QString line, DebugLevel debugLevel, QString file)
{
    pDebug(line, 0, debugLevel, file);
}


void MainWindow::pDebug(QString line, qint64 numLine, DebugLevel debugLevel, QString file)
{
    (void)debugLevel;
    QString logLine = "";
    QString timeStamp = QDateTime::currentDateTime().toString("hh:mm:ss");

    while(line.length() > 0 && line[0]==QChar('\n'))
    {
        line.remove(0, 1);
        logLine += '\n';
    }

    if(!line.isEmpty())
    {
        logLine += timeStamp + " - " + file;
        if(numLine > 0) logLine += "(" + QString::number(numLine) + ")";
        logLine += ": " + line;
    }

    qDebug().noquote() << logLine;

    if(atLogFile != nullptr)
    {
        QTextStream stream(atLogFile);
        stream << logLine << Qt::endl;
    }
}


void MainWindow::logReset()
{
    leaveArena();
    gameWatcher->reset();
}


bool MainWindow::checkCardImage(QString code)
{
    if(code.isEmpty())  return true;

    QFileInfo cardFile(Utility::hscardsPath() + "/" + code + ".png");

    if(!cardFile.exists())
    {
        //La bajamos de HearthstoneJSON/Hearthpwn
        cardDownloader->downloadWebImage(code);
        return false;
    }
    return true;
}


void MainWindow::redrawDownloadedCardImage(QString code)
{
    draftHandler->reHistDownloadedCardImage(code);
    if(!allCardsDownloadList.isEmpty())     this->updateProgressAllCardsDownload(code);
}


void MainWindow::missingOnWeb(QString code)
{
    draftHandler->reHistDownloadedCardImage(code, true);
    if(!allCardsDownloadList.isEmpty())     this->updateProgressAllCardsDownload(code);
}


void MainWindow::createLogFile()
{
    QString logPath = Utility::dataPath() + "/ArenaDudeLog.txt";
    QString logOldPath = Utility::dataPath() + "/ArenaDudeLog.old";

    //Copy log from previous session
    QFile::remove(logOldPath);
    QFile::rename(logPath, logOldPath);

    atLogFile = new QFile(logPath);
    if(atLogFile->exists())  atLogFile->remove();
    if(!atLogFile->open(QIODevice::WriteOnly | QIODevice::Text))
    {
        pDebug("Failed to create Arena Dude log on disk.", DebugLevel::Error);
        atLogFile = nullptr;
    }
}


void MainWindow::closeLogFile()
{
    if(atLogFile == nullptr)   return;
    atLogFile->close();
    delete atLogFile;
    atLogFile = nullptr;
}


void MainWindow::createDataDir()
{
    Utility::createDir(Utility::dataPath());
    if(REMOVE_CARDS_ON_VERSION_UPDATE)  removeHSCards();//Redownload HSCards en esta version
    if(REMOVE_EXTRA_AND_HISTOGRAMS_ON_VERSION_UPDATE)  removeExtraAndHistograms();//Redownload Extra en esta version y recrea histogramas
    if(Utility::createDir(Utility::hscardsPath()))  allCardsDownloadNeeded = true;
    Utility::createDir(Utility::extraPath());
    Utility::createDir(Utility::histogramsPath());
    Utility::createDir(Utility::arenaStatsPath());
}


void MainWindow::downloadExtraFile(QString nameFile)
{
    QFileInfo file = QFileInfo(Utility::extraPath() + "/" + nameFile);
    if(!file.exists())  networkManager->get(QNetworkRequest(QUrl(EXTRA_URL + QString("/") + nameFile)));
}


void MainWindow::downloadExtraFiles()
{
    downloadExtraFile("arenaTemplate.png");
    downloadExtraFile("arenaTemplate2.png");
    downloadExtraFile("heroesTemplate.png");
    downloadExtraFile("heroesTemplate2.png");
    downloadExtraFile("redraftTemplate.png");
    downloadExtraFile("MANA.dat");
    downloadExtraFile("RARITY.dat");


    QFileInfo file = QFileInfo(Utility::extraPath() + "/icon.png");
    if(!file.exists())  networkManager->get(QNetworkRequest(QUrl(IMAGES_URL + QString("/icon.png"))));
}


void MainWindow::downloadHearthArenaVersion()
{
    networkManager->get(QNetworkRequest(QUrl(HA_URL + QString("/haVersion.json"))));
}


void MainWindow::downloadHearthArenaJson(int version)
{
    bool needDownload = false;
    QSettings settings;
    int storedVersion = settings.value("haVersion", 0).toInt();

    QFileInfo fileInfo(Utility::extraPath() + "/hearthArena.json");
    if(!fileInfo.exists())          needDownload = true;
    if(version != storedVersion)    needDownload = true;

    pDebug("Extra: Json HearthArena: Local(" + QString::number(storedVersion) + ") - "
                        "Web(" + QString::number(version) + ")" + (!needDownload?" up-to-date":""));

    if(needDownload)
    {
        if(fileInfo.exists())
        {
            QFile file(Utility::extraPath() + "/hearthArena.json");
            file.remove();
            pDebug("Extra: Json HearthArena removed.");
        }

        //Remove histograms
        removeHistograms();
        Utility::createDir(Utility::histogramsPath());

        settings.setValue("haVersion", version);
        networkManager->get(QNetworkRequest(QUrl(HA_URL + QString("/hearthArena.json"))));
        pDebug("Extra: Json HearthArena --> Download from: " + QString(HA_URL) + QString("/hearthArena.json"));
    }
}


void MainWindow::downloadCardsJsonVersion()
{
    networkManager->get(QNetworkRequest(QUrl(CARDS_URL + QString("/cardsVersion.json"))));
}


void MainWindow::downloadCardsJson(int version)
{
    bool needDownload = false;
    QSettings settings;
    int storedVersion = settings.value("cardsVersion", 0).toInt();

    QFileInfo fileInfo(Utility::extraPath() + "/cards.json");
    if(!fileInfo.exists())          needDownload = true;
    if(version != storedVersion)    needDownload = true;

    pDebug("Extra: Json Cards: Local(" + QString::number(storedVersion) + ") - "
                        "Web(" + QString::number(version) + ")" + (!needDownload?" up-to-date":""));

    if(needDownload)
    {
        settings.setValue("cardsVersion", version);
        networkManager->get(QNetworkRequest(QUrl(CARDS_URL + QString("/cards.json"))));
        pDebug("Extra: Json Cards --> Download from: " + QString(CARDS_URL) + QString("/cards.json"));
    }
    else
    {
        Utility::setCardsJsonUpToDate(true);
        checkArenaCards();
        initWRCards();
    }
}


void MainWindow::removeHSCards(bool forceRemove)
{
    QSettings settings;
    QString runVersion = settings.value("runVersion", "").toString();

    if(runVersion != VERSION || forceRemove)
    {
        QDir cardsDir = QDir(Utility::hscardsPath());
        cardsDir.removeRecursively();
        pDebug(Utility::hscardsPath() + " removed.");
    }
}


void MainWindow::removeExtraAndHistograms()
{
    QSettings settings;
    QString runVersion = settings.value("runVersion", "").toString();

    if(runVersion != VERSION)
    {
        QDir extraDir = QDir(Utility::extraPath());
        extraDir.removeRecursively();
        pDebug(Utility::extraPath() + " removed.");

        removeHistograms();
    }
}


void MainWindow::removeHistograms()
{
    QDir dir = QDir(Utility::histogramsPath());
    dir.removeRecursively();
    pDebug(Utility::histogramsPath() + " removed.");
}


void MainWindow::completeArenaDeck()
{
    if(draftHandler == nullptr || deckHandler == nullptr)   return;

    deckHandler->completeArenaDeck(Utility::classEnum2classLogNumber(draftHandler->getArenaHero()));
}


LoadingScreenState MainWindow::getLoadingScreen()
{
    if(gameWatcher != nullptr) return gameWatcher->getLoadingScreen();
    else                    return menu;
}


void MainWindow::checkFirstRunNewVersion()
{
    QSettings settings;
    QString runVersion = settings.value("runVersion", "").toString();

    if(runVersion != VERSION)
    {
        pDebug("First run of new version.");
        settings.setValue("neoInt", 0);
    }
}


//Solo baja las cartas si arenaVersion ha cambiado de version o HSCards se ha borrado
void MainWindow::checkArenaCards()
{
    if(draftHandler == nullptr || !cardsJsonLoaded || !arenaSetsLoaded ||
        !Utility::isCardsJsonUpToDate())    return;

    QSettings settings;

    if(allCardsDownloadNeeded)
    {
        settings.setValue("allCardsDownloaded", false);
        QStringList codeList = Utility::getAllArenaCodes();
        downloadAllArenaCodes(codeList);
    }
    else
    {
        pDebug("CheckArenaCards: No arena cards downloads.");
        QTimer::singleShot(0, this, &MainWindow::startupReady);     //After main() connects the splash
    }
}


void MainWindow::downloadAllArenaCodes(const QStringList &codeList)
{
    pDebug("CheckArenaCards: Downloading all arena cards.");
    allCardsDownloadStarted = true;
    allCardsDownloadList.clear();

    for(const QString &code: codeList)
    {
        if(!checkCardImage(code))
        {
            allCardsDownloadList.append(code);
        }
        //FALSO: Solo bajamos golden cards de cartas colleccionables
        if(/*Utility::getCardAttribute(code, "collectible").toBool() &&*/
            !checkCardImage(code + "_premium"))
        {
            allCardsDownloadList.append(code + "_premium");
        }
    }

    if(allCardsDownloadList.isEmpty())  this->allCardsDownloaded();
    else
    {
        allCardsDownloadTotal = allCardsDownloadList.count();
        QTimer::singleShot(0, this, [this]() { emit startupProgress(0, allCardsDownloadTotal); });
    }
}


void MainWindow::updateProgressAllCardsDownload(QString code)
{
    if(allCardsDownloadList.removeOne(code))
    {
        emit startupProgress(allCardsDownloadTotal - allCardsDownloadList.count(), allCardsDownloadTotal);
    }
}


void MainWindow::allCardsDownloaded()
{
    //The download queue also empties after the cards of the deck, before downloadAllArenaCodes() started
    if(!allCardsDownloadStarted)    return;

    QTimer::singleShot(0, this, &MainWindow::startupReady);     //After main() connects the splash
    QSettings settings;

    if(allCardsDownloadNeeded)
    {
        settings.setValue("allCardsDownloaded", true);
        allCardsDownloadNeeded = false;
        allCardsDownloadStarted = false;
        allCardsDownloadList.clear();
        pDebug("CheckArenaCards: All arena cards have been downloaded.");
    }
}
