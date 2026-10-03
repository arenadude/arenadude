#include "gamewatcher.h"
#include <QtWidgets>


GameWatcher::GameWatcher(QObject *parent) : QObject(parent)
{
    reset();
    match = new QRegularExpressionMatch();
}


GameWatcher::~GameWatcher()
{
    delete match;
}


void GameWatcher::reset()
{
    powerState = noGame;
    arenaState = noDeckRead;
    loadingScreenState = menu;
    mulliganEnemyDone = mulliganPlayerDone = false;
    spectating = false;
    tied = true;
    emit pDebug("Reset (powerState = noGame).", 0);
    emit pDebug("Reset (LoadingScreen = menu).", 0);
}


void GameWatcher::processLogLine(LogComponent logComponent, QString line, qint64 numLine, qint64 logSeek)
{
    Q_UNUSED(logSeek);

    switch(logComponent)
    {
        case logPower:
            processPower(line, numLine);
        break;
        case logZone:
            processZone(line, numLine);
        break;
        case logLoadingScreen:
            processLoadingScreen(line, numLine);
        break;
        case logArena:
            processArena(line, numLine);
        break;
        case logAsset:
            processAsset(line, numLine);
        break;
        case logInvalid:
            emit pDebug("Unknown log component read.", Warning);
        break;
    }
}


void GameWatcher::startReadingDeck()
{
    if(arenaState == deckRead) return;
    emit needResetDeck();    //resetDeck
    arenaState = readingDeck;
    emit pDebug("Start reading deck (arenaState = readingDeck).", 0);
}


//The cards Hearthstone lists for the current deck, to correct the tracker's: the cards discarded in a redraft are
//read by OCR on the discard screen, which can be wrong
void GameWatcher::emitDeckSnapshot()
{
    if(deckSnapshotSync && !deckSnapshot.isEmpty())
    {
        emit pDebug("Deck snapshot: " + QString::number(deckSnapshot.count()) + " cards.", 0);
        emit deckSnapshotRead(deckSnapshot);
    }
    deckSnapshotSync = false;
    deckSnapshot.clear();
}


void GameWatcher::endReadingDeck()
{
    if(arenaState != readingDeck)    return;
    arenaState = deckRead;
    emit arenaDeckRead();   //completeArenaDeck with draft file
    emit pDebug("End reading deck (arenaState = deckRead).", 0);
}


void GameWatcher::setDeckRead(bool value)
{
    if(value)
    {
        arenaState = deckRead;
        emit pDebug("SetDeckRead (arenaState = deckRead).", 0);
    }
    else
    {
        arenaState = noDeckRead;
        emit pDebug("SetDeckRead (arenaState = noDeckRead).", 0);
    }
}


void GameWatcher::processLoadingScreen(QString &line, qint64 numLine)
{
    static QString lastMode = "";

    //[LoadingScreen] LoadingScreen.OnSceneLoaded() - prevMode=HUB currMode=DRAFT
    if(line.contains(QRegularExpression("LoadingScreen\\.OnSceneLoaded\\(\\) *- *prevMode=(\\w+) *currMode=(\\w+)"), match))
    {
        QString prevMode = match->captured(1);
        QString currMode = match->captured(2);
        emit pDebug("\nLoadingScreen: " + prevMode + " -> " + currMode, numLine);

        //Create result, avoid first run
        if(prevMode == "GAMEPLAY" && prevMode == lastMode)
        {
            if(spectating || loadingScreenState == menu || tied)
            {
                emit pDebug("CreateGameResult: Avoid spectator/tied game result.", 0);
            }
            else
            {
                createGameResult();
            }
            spectating = false;
        }

        if(currMode == lastMode)
        {
            emit pDebug("Avoid double LoadingScreen.", numLine);
            return;
        }
        else
        {
            lastMode = currMode;
        }

        if(currMode == "DRAFT")
        {
            loadingScreenState = arena;
            emit pDebug("Entering ARENA (loadingScreenState = arena).", numLine);
            emit checkRedraft();//checkRedraft drafthandler
            //Al encontrar SetDraftMode - REDRAFTING marcamos redrafting=true en drafthandler y al entrar en arena hacemos continueDraft()
            //Si hacemos el draft en SetSetDraftMode - REDRAFTING, despues ocurrira un LoadingScreen: GAMEPLAY -> DRAFT que reiniciara
            //los contadores de drafthandler para calcular el score del deck enemigo en createGameResult() creando caos. De este modo no
            //empezamos el nuevo draft hasta que createGameResult() ha terminado.

            if(prevMode == "HUB" || prevMode == "FRIENDLY")
            {
                emit enterArena();//enterArena deckHandler
            }
        }
        else if(currMode == "HUB")
        {
            loadingScreenState = menu;
            emit pDebug("Entering MENU (loadingScreenState = menu).", numLine);

            if(prevMode == "DRAFT")
            {
                setDeckRead(false);
                emit leaveArena();//leaveArena deckHandler
            }
        }
        else if(currMode == "GAME_MODE")
        {
            loadingScreenState = gameMode;
            emit pDebug("Entering GAME_MODE (loadingScreenState = gameMode).", numLine);
        }
        else if(currMode == "TOURNAMENT")
        {
            loadingScreenState = ranked;
            emit pDebug("Entering CASUAL/RANKED (loadingScreenState = ranked).", numLine);
        }
        else if(currMode == "ADVENTURE")
        {
            loadingScreenState = adventure;
            emit pDebug("Entering ADVENTURE (loadingScreenState = adventure).", numLine);
        }
        else if(currMode == "TAVERN_BRAWL")
        {
            loadingScreenState = tavernBrawl;
            emit pDebug("Entering TAVERN (loadingScreenState = tavernBrawl).", numLine);
        }
        else if(currMode == "FRIENDLY")
        {
            loadingScreenState = friendly;
            emit pDebug("Entering FRIENDLY (loadingScreenState = friendly).", numLine);

            if(prevMode == "DRAFT")
            {
                setDeckRead(false);
                emit leaveArena();//leaveArena deckHandler
            }
        }
    }
}


void GameWatcher::processAsset(QString &line, qint64 numLine)
{
    Q_UNUSED(line);
    Q_UNUSED(numLine);

//    if(powerState != noGame)   return;

//    //Definimos RANKED solo si venimos de loadScreen TOURNAMENT y
//    //acabamos de encontrar el WON pero aun no hemos createResult
//    if(loadingScreenState == casual && logSeekWon != -1 && line.contains("assetPath=rank_window"))
//    {
//        loadingScreenState = ranked;
//        emit pDebug("On RANKED (loadingScreenState = ranked).", numLine);
//    }
}


//Usar ticket arena/Comprar arena
//D 11:27:13.1732560 DraftManager.OnBegin - Got new draft deck with ID: 507495951
//D 11:27:13.1736650 SetDraftMode - DRAFTING
//Volver arena eleccion heroes ya comprada
//D 11:27:47.0196860 DraftManager.OnChoicesAndContents - Draft Deck ID: 507495951, Hero Card =
//D 11:27:47.0197460 SetDraftMode - DRAFTING
void GameWatcher::processArena(QString &line, qint64 numLine)
{
    //NEW ARENA - START DRAFT
    //[Arena] DraftManager.OnChosen(): hero=HERO_02 premium=STANDARD
    if(line.contains(QRegularExpression("DraftManager\\.OnChosen\\(\\): hero=HERO_(\\d+)"), match))
    {
        QString hero = match->captured(1);
        emit pDebug("New arena. Heroe: " + hero, numLine);
        emit newArena(hero); //(connect)Begin draft //(connect)resetDeckDontRead (arenaState = deckRead)
    }
    //DRAFTING PICK CARD
    //[Arena] Client chooses: Profesora violeta (NEW1_026)
    else if(line.contains(QRegularExpression("Client chooses: .* \\((\\w+)\\)"), match))
    {
        QString code = match->captured(1);
        emit pDebug("Pick card: " + code, numLine);
        emit pickCard(code);
    }
    //START READING DECK
    //[Arena] DraftManager.OnChoicesAndContents - Draft Deck ID: 472720132, Hero Card = HERO_02
    else if(line.contains(QRegularExpression(
                "DraftManager\\.OnChoicesAndContents - Draft Deck ID: \\d+, Hero Card = HERO_(\\d+)"), match))
    {
        QString hero = match->captured(1);
        emit pDebug("Found Hero Draft Deck. Heroe: " + hero, numLine);
        //The deck is read once per arena visit: later snapshots (after games, before a redraft) only correct it.
        //The one right after OnRedraftBegin is the deck before the redraft picks: skipped.
        deckSnapshot.clear();
        deckSnapshotSync = (arenaState == deckRead) && !redraftBeginSeen;
        redraftBeginSeen = false;
        startReadingDeck();
        emit heroDraftDeck(hero);
    }
    //END READING DECK
    //[Arena] SetDraftMode - ACTIVE_DRAFT_DECK
    else if(line.contains("SetDraftMode - ACTIVE_DRAFT_DECK"))
    {
        emit pDebug("Found ACTIVE_DRAFT_DECK.", numLine);
        emitDeckSnapshot();
        endReadingDeck();//completeArenaDeck with draft file
        emit activeDraftDeck(); //(connect)End draft/Show mechanics, debe estar detras de endReadingDeck
        //para primero completar el deck y luego mostrar la mechanics window del deck completo
    }
    //READ DECK CARD
    //[Arena] DraftManager.OnChoicesAndContents - Draft deck contains card FP1_012
    else if(line.contains(QRegularExpression(
            "DraftManager\\.OnChoicesAndContents - Draft deck contains card (\\w+)"), match))
    {
        QString code = match->captured(1);
        if(arenaState == readingDeck)
        {
            emit pDebug("Reading deck: " + code, numLine);
            emit newDeckCard(code);
        }
        else if(deckSnapshotSync)   deckSnapshot << code;
    }
    //[Arena] DraftManager.OnRedraftBegin - Got new redraft deck with ID: 2769477640
    else if(line.contains("DraftManager.OnRedraftBegin"))
    {
        redraftBeginSeen = true;
    }
    //COMPRAR ARENA -- VUELTA A SELECCION HEROE
    else if(line.contains(QRegularExpression(
                "DraftManager\\.OnBegin - Got new draft deck with ID: \\d+"), match) ||
            line.contains(QRegularExpression(
                            "DraftManager\\.OnChoicesAndContents - Draft Deck ID: \\d+, Hero Card ="), match))
    {
        emit pDebug("New arena: choosing heroe.", numLine);
        emit heroDraftDeck();//No hero
        emit arenaChoosingHeroe();  //(connect) beginHeroDraft
    }
    //SetDraftMode - DRAFTING
    else if(line.contains("SetDraftMode - DRAFTING"))
    {
        emit pDebug("Found SetDraftMode - DRAFTING.", numLine);
        emit continueDraft();   //(connect) continueDraft
    }
    //SetDraftMode - REDRAFTING
    else if(line.contains("SetDraftMode - REDRAFTING"))
    {
        emit pDebug("Found SetDraftMode - REDRAFTING.", numLine);
        emitDeckSnapshot();
        endReadingDeck();//completeArenaDeck with draft file
        emit redraft();
    }
    //DraftManager.OnRetire deckID=2769165728 (right before IN_REWARDS)
    else if(line.contains("DraftManager.OnRetire"))
    {
        emit pDebug("Found DraftManager.OnRetire.", numLine);
        emit arenaRetired();
    }
    //SetDraftMode - IN_REWARDS
    else if(line.contains("SetDraftMode - IN_REWARDS"))
    {
        emit pDebug("Found SetDraftMode - IN_REWARDS.", numLine);
        emit heroDraftDeck();//No hero
        emit inRewards();   //Remove mechanics window
    }
}


//Ejemplo spectate
//11:22:01 - GameWatcher(1): Start Spectator Game.
//11:22:01 - GameWatcher(3): Found CREATE_GAME (powerState = heroType1State)
//11:22:04 - GameWatcher(22): LoadingScreen: HUB -> GAMEPLAY
//Juego arena
//20:58:25 - GameWatcher(27229): Found WON (powerState = noGame): Dappo
//20:58:35 - GameWatcher(27255): End Spectator Game.
//20:58:36 - GameWatcher(346): LoadingScreen: GAMEPLAY -> HUB
//20:58:36 - GameWatcher: CreateGameResult: Avoid spectator/tied game result.
//20:58:36 - GameWatcher(346): Entering MENU (loadingScreenState = menu).
//Nuevo juego arena
//20:58:54 - GameWatcher(27256): Start Spectator Game.
//20:58:56 - GameWatcher(27258): Found CREATE_GAME (powerState = heroType1State)
//20:58:56 - GameWatcher(376): LoadingScreen: HUB -> GAMEPLAY

void GameWatcher::processPower(QString &line, qint64 numLine)
{
    //================== End Spectator Game ==================
    if(line.contains("End Spectator Game"))
    {
        emit pDebug("End Spectator Game.", numLine);
//        spectating = false;//Se pondra a false despues de haberse creado el resultado en LoadingScreen: GAMEPLAY -> HUB

        if(powerState != noGame)
        {
            emit pDebug("WON not found (PowerState = noGame)", 0);
            powerState = noGame;
            emit endGame();
        }
    }

    //================== Begin Spectating 1st player ==================
    //================== Start Spectator Game ==================
    else if(line.contains("Begin Spectating") || line.contains("Start Spectator Game"))
    {
        emit pDebug("Start Spectator Game.", numLine);
        spectating = true;
    }
    //Create game
    //GameState.DebugPrintPower() - CREATE_GAME
    else if(line.contains("GameState.DebugPrintPower() - CREATE_GAME"))
    {
        if(powerState != noGame)
        {
            emit pDebug("WON not found (PowerState = noGame)", 0);
            powerState = noGame;
            emit endGame();
        }

        emit pDebug("\nFound CREATE_GAME (powerState = heroType1State)", numLine);
        powerState = heroType1State;

        mulliganEnemyDone = mulliganPlayerDone = false;

        hero1.clear();
        hero2.clear();
        name1.clear();
        name2.clear();
        firstPlayer.clear();
        winnerPlayer.clear();
        playerID = 0;
        playerTag.clear();
        tied = true;//Si no se encuentra WON no se llamara a createGameResult() pq tried sigue siendo true

        emit startGame();
    }

    if(powerState != noGame)
    {
        //Win state
        //PowerTaskList.DebugPrintPower() -     TAG_CHANGE Entity=El tabernero tag=PLAYSTATE value=WON
        if(line.contains(QRegularExpression(
                            "PowerTaskList\\.DebugPrintPower\\(\\) - *TAG_CHANGE "
                            "Entity=(.+) tag=PLAYSTATE value=(WON|TIED)"), match))
        {
            winnerPlayer = match->captured(1);
            tied = (match->captured(2) == "TIED");
            powerState = noGame;
            if(tied)    emit pDebug("Found TIED (powerState = noGame)", numLine);
            else        emit pDebug("Found WON (powerState = noGame): " + winnerPlayer + (playerTag.isEmpty()?" - Unknown winner":""), numLine);

            bool playerWon = !tied && (winnerPlayer == playerTag);
            emit endGame(playerWon, playerTag.isEmpty());
        }
        //Turn
        //PowerTaskList.DebugPrintPower() -     TAG_CHANGE Entity=GameEntity tag=TURN value=12
        else if(line.contains(QRegularExpression(
                            "PowerTaskList\\.DebugPrintPower\\(\\) - *TAG_CHANGE "
                            "Entity=GameEntity tag=TURN value=(\\d+)"
                ), match))
        {
            const int turn = match->captured(1).toInt();
            emit pDebug("Found TURN: " + match->captured(1), numLine);

            if(powerState != inGameState && turn > 1)
            {
                powerState = inGameState;
                mulliganEnemyDone = mulliganPlayerDone = true;
                emit pDebug("WARNING: Heroes/Players info missing (powerState = inGameState, mulliganDone = true)", 0, Warning);
            }
        }
    }
    switch(powerState)
    {
        case noGame:
            break;
        case heroType1State:
        case heroPower1State:
        case heroType2State:
            processPowerHero(line, numLine);
            break;
        case mulliganState:
            processPowerMulligan(line, numLine);
            break;
        case inGameState:
            break;
    }
}


void GameWatcher::processPowerHero(QString &line, qint64 numLine)
{
    if(powerState == heroPower1State)
    {
        if(line.contains(QRegularExpression("Creating ID=\\d+ CardID=(\\w+)"), match))
        {
            powerState = heroType2State;
            QString hp1 = match->captured(1);
            emit pDebug("Skip hero power 1: " + hp1 + " (powerState = heroType2State)", numLine);
        }
    }
    else// powerState == heroType1State || powerState == heroType2State
    {
        if(line.contains(QRegularExpression("Creating ID=\\d+ CardID=HERO_(\\d+)"), match))
        {
            if(powerState == heroType1State)
            {
                powerState = heroPower1State;
                hero1 = match->captured(1);
                emit pDebug("Found hero 1: " + hero1 + " (powerState = heroPower1State)", numLine);
            }
            else //if(powerState == heroType2State
            {
                powerState = mulliganState;
                hero2 = match->captured(1);
                emit pDebug("Found hero 2: " + hero2 + " (powerState = mulliganState)", numLine);
            }
        }
    }
}


void GameWatcher::processPowerMulligan(QString &line, qint64 numLine)
{
    //Jugador/Enemigo names, playerTag y firstPlayer
    //GameState.DebugPrintEntityChoices() - id=1 Player=UNKNOWN HUMAN PLAYER TaskList= ChoiceType=MULLIGAN CountMin=0 CountMax=3
    //GameState.DebugPrintEntityChoices() - id=2 Player=triodo#2541 TaskList= ChoiceType=MULLIGAN CountMin=0 CountMax=5
    if(line.contains(QRegularExpression(
                "GameState\\.DebugPrintEntityChoices\\(\\) - id=(\\d+) Player=(.*) TaskList=\\d* ChoiceType=MULLIGAN CountMin=0 CountMax=(\\d+)"
                  ), match))
    {
        QString player = match->captured(1);
        QString playerName = match->captured(2);
        QString numCards = match->captured(3);

        if(player.toInt() == 1)
        {
            emit pDebug("Found player 1: " + playerName, numLine);
            if(playerName != "UNKNOWN HUMAN PLAYER")    name1 = playerName;
        }
        else if(player.toInt() == 2)
        {
            emit pDebug("Found player 2: " + playerName, numLine);
            if(playerName != "UNKNOWN HUMAN PLAYER")    name2 = playerName;
        }
        else    emit pDebug("Read invalid PlayerID value: " + player, numLine, DebugLevel::Error);

        //No se usa. El playerID se calcula (junto al playerTag) al cargar el retrato del heroe en processZone.
        if(playerTag.isEmpty() && playerID == player.toInt())
        {
            playerTag = (playerID == 1)?name1:name2;
            emit pDebug("Found playerTag: " + playerTag, numLine);
        }

        if(numCards == "3")
        {
            firstPlayer = playerName;
            emit pDebug("Found First Player: " + firstPlayer, numLine);
        }
    }


    //MULLIGAN DONE
    //GameState.DebugPrintPower() -     TAG_CHANGE Entity=fayatime tag=MULLIGAN_STATE value=DONE
    //GameState.DebugPrintPower() -     TAG_CHANGE Entity=Винсент tag=MULLIGAN_STATE value=DONE
    else if(line.contains(QRegularExpression("Entity=(.+) tag=MULLIGAN_STATE value=DONE"
            ), match))
    {
        QString entityName = match->captured(1);
        //Player mulligan
        if(entityName == playerTag)
        {
            if(!mulliganPlayerDone)
            {
                emit pDebug("Player mulligan end.", numLine);
                mulliganPlayerDone = true;

                if(mulliganEnemyDone)
                {
                    //turn = 1;
                    powerState = inGameState;
                    emit pDebug("Mulligan phase end (powerState = inGameState)", numLine);
                }
            }
        }
        //Enemy mulligan
        else
        {
            if(!mulliganEnemyDone)
            {
                emit pDebug("Enemy mulligan end.", numLine);
                mulliganEnemyDone = true;

                //Revisamos Enemy name, por si cogio "UNKNOWN HUMAN PLAYER" al inicio, en cuyo caso name1/name2 = ""
                QString enemyName = (playerID == 1)?name2:name1;
                if(enemyName.isEmpty() && !entityName.isEmpty())
                {
                    if(playerID == 1)   name2 = entityName;
                    else                name1 = entityName;
                }

                if(mulliganPlayerDone)
                {
                    //turn = 1;
                    powerState = inGameState;
                    emit pDebug("Mulligan phase end (powerState = inGameState)", numLine);
                }
            }
        }
    }
}


//Only what the mascot needs: an enemy secret, and the player's hero, which tells which player we are (who won)
void GameWatcher::processZone(QString &line, qint64 numLine)
{
    if(powerState == noGame)   return;

    //[entityName=UNKNOWN ENTITY [cardType=INVALID] id=69 zone=SECRET zonePos=0 cardId= player=2] zone from OPPOSING HAND -> OPPOSING SECRET
    if(line.contains(QRegularExpression(
        "\\[entityName=UNKNOWN ENTITY \\[cardType=INVALID\\] id=(\\d+) zone=\\w+ zonePos=\\d+ cardId= player=\\d+\\] zone from "
        "(\\w+ \\w+(?: \\(Weapon\\))?)? -> OPPOSING SECRET"
        ), match))
    {
        emit pDebug("Enemy: Secret played. ID: " + match->captured(1), numLine);
        emit enemySecretPlayed();
    }

    //[entityName=Jaina id=64 zone=PLAY zonePos=0 cardId=HERO_08 player=1] zone from  -> FRIENDLY PLAY (Hero)
    else if(playerID == 0 && line.contains(QRegularExpression(
        "\\[entityName=(.*) id=\\d+ zone=\\w+ zonePos=\\d+ cardId=\\w* player=(\\d+)\\] zone from "
        ".* -> FRIENDLY PLAY \\(Hero\\)"
        ), match))
    {
        playerID = match->captured(2).toInt();
        emit pDebug("Player: Hero moved to FRIENDLY PLAY (Hero): " + match->captured(1), numLine);
        emit pDebug("Found playerID: " + match->captured(2), numLine);

        if(playerTag.isEmpty())
        {
            playerTag = (playerID == 1)?name1:name2;
            if(!playerTag.isEmpty())    emit pDebug("Found playerTag: " + playerTag, numLine);
        }
    }
}


void GameWatcher::createGameResult()
{
    GameResult gameResult;

    if(playerID == 1)
    {
        gameResult.playerHero = hero1;
        gameResult.enemyHero = hero2;
        gameResult.enemyName = name2;
    }
    else if(playerID == 2)
    {
        gameResult.playerHero = hero2;
        gameResult.enemyHero = hero1;
        gameResult.enemyName = name1;
    }
    else
    {
        emit pDebug("CreateGameResult: PlayerID wasn't defined in the game.", 0, DebugLevel::Error);
        return;
    }

    gameResult.isFirst = (firstPlayer == playerTag);
    gameResult.isWinner = (winnerPlayer == playerTag);

    emit newGameResult(gameResult, loadingScreenState);

    //Save player tag
    QString playerTagPreSharp = getNamePreSharp(playerTag);
    if(playerTagPreSharp.isEmpty())
    {
        emit pDebug("Avoid save empty playerName: " + playerTagPreSharp + "(" + playerTag + ")", 0);
    }
    else
    {
        emit pDebug("Save playerName: " + playerTagPreSharp + "(" + playerTag + ")", 0);
        QSettings settings;
        settings.setValue("playerName", playerTagPreSharp);
    }
}


QString GameWatcher::getNamePreSharp(QString name)
{
    return name.split("#").first();
}


LoadingScreenState GameWatcher::getLoadingScreen()
{
    return this->loadingScreenState;
}

