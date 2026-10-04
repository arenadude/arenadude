#include "drafthandler.h"
#include "Utils/pickrating.h"
#include <pthread/qos.h>
#include <QtConcurrent/QtConcurrent>
#include <QtWidgets>
    #include "Utils/macocr.h"
    #include "Utils/macwindow.h"


//The screenshot of screenRect shows the tracker's own windows over Hearthstone too (the mascot's bubble lists card
//names): they are painted black before the OCR reads it
static void hideTrackerWindows(QImage &image, const QRect &screenRect)
{
    if(image.isNull() || screenRect.isEmpty())  return;
    const qreal scale = image.width() / static_cast<qreal>(screenRect.width());
    QPainter painter(&image);
    for(QWidget *widget: QApplication::topLevelWidgets())
    {
        if(!widget->isVisible())    continue;
        QRect area = widget->frameGeometry() & screenRect;
        if(area.isEmpty())  continue;
        area.translate(-screenRect.topLeft());
        painter.fillRect(QRectF(area.x()*scale, area.y()*scale, area.width()*scale, area.height()*scale), Qt::black);
    }
}


DraftHandler::DraftHandler(QObject *parent, DeckHandler *deckHandler) : QObject(parent)
{
    this->deckHandler = deckHandler;
    this->deckRatingHA = 0;
    this->deckRatingFire = 0;
    this->numCaptured = 0;
    this->extendedCapture = false;
    this->drafting = false;
    this->redrafting = false;
    this->redraftingReview = false;
    this->heroDrafting = false;
    this->capturing = false;
    this->findingFrame = false;
    this->stopLoops = true;
    this->draftHeroWindow = nullptr;
    this->draftScoreWindow = nullptr;
    this->draftMethodHA = false;
    this->draftMethodFire = true;
    this->multiclassArena = false;
    this->showMyWR = true;
    this->fireWRMap = nullptr;
    this->fireSamplesMap = nullptr;
    this->screenIndex = -1;
    this->screenScale = QPointF(1,1);
    this->needSaveCardHist = false;
    this->cardsJsonWaits = 0;
    this->prevCodesTime = 0;

    for(int i=0; i<3; i++)
    {
        cardDetected[i] = false;
    }
    for(int i=0; i<5; i++)
    {
        screenRects[i] = cv::Rect(0,0,0,0);
        manaRects[i] = cv::Rect(0,0,0,0);
        rarityRects[i] = cv::Rect(0,0,0,0);
    }

    findScreenFails = 0;

    connect(&futureFindScreenRects, SIGNAL(finished()), this, SLOT(finishFindScreenRects()));
    connect(&futureReviewBestCards, SIGNAL(finished()), this, SLOT(finishReviewBestCards()));

    redraftWatchTimer = new QTimer(this);
    redraftWatchTimer->setInterval(REDRAFT_WATCH_TIME);
    connect(redraftWatchTimer, SIGNAL(timeout()), this, SLOT(checkRedraftScreen()));
    connect(&futureRedraftCounter, SIGNAL(finished()), this, SLOT(finishCheckRedraftScreen()));
    connect(&futureRewardsWins, SIGNAL(finished()), this, SLOT(finishReadRewardsWins()));
    connect(&futureReadyUpWins, SIGNAL(finished()), this, SLOT(finishReadReadyUpWins()));

    bundlePending = bundlePreviewVisible = bundlePreviewOpen = false;
    bundleMisses = bundleReads = 0;
    bundleTimer = new QTimer(this);
    bundleTimer->setInterval(REDRAFT_REVIEW_OCR_TIME);
    connect(bundleTimer, SIGNAL(timeout()), this, SLOT(captureBundlePreview()));
    connect(&futureBundle, SIGNAL(finished()), this, SLOT(finishBundlePreview()));
    connect(&futureDeckList, SIGNAL(finished()), this, SLOT(finishDeckList()));

    redraftReviewTimer = new QTimer(this);
    redraftReviewTimer->setInterval(REDRAFT_REVIEW_OCR_TIME);
    connect(redraftReviewTimer, SIGNAL(timeout()), this, SLOT(captureRedraftReviewNames()));
    connect(&futureRedraftReviewCodes, SIGNAL(finished()), this, SLOT(finishRedraftReviewNames()));
}

DraftHandler::~DraftHandler()
{
    deleteDraftHeroWindow();
    deleteDraftScoreWindow();
}


//Empty text clears the status
void DraftHandler::setDraftStatus(const QString &text)
{
    //Waiting on the Ready Up screen for the offered redraft: "Scanning cards..." or "Can't see the arena" would be wrong there
    if(!text.isEmpty() && isRedraftOffered())   return;
    draftStatus = text;
    emit draftStatusChanged(text);
}


void DraftHandler::setMulticlassArena(bool multiclassArena)
{
    this->multiclassArena = multiclassArena;
}


CardClass DraftHandler::getArenaHero()
{
    return arenaHero;
}


void DraftHandler::initHearthArenaTiers(const CardClass heroClass, const bool multiClassDraft)
{
    hearthArenaTiers.clear();
    QJsonObject jsonObj = Utility::loadHearthArena();

    const QString &heroString = Utility::classOrder2classUL_ULName(heroClass);

    if(multiClassDraft)
    {
        QJsonObject heroJsonObject = jsonObj.value(heroString).toObject();
        QJsonObject othersJsonObject[NUM_HEROS-1];
        for(int j=0, i=0; j<NUM_HEROS; j++)
        {
            if(heroClass != j)
            {
                othersJsonObject[i++] = jsonObj.value(Utility::classOrder2classUL_ULName(j)).toObject();
            }
        }
        const QStringList lfKeys = lightForgeTiers.keys();
        for(const QString &code: lfKeys)
        {
            if(heroJsonObject.contains(code))
            {
                hearthArenaTiers.insert(code, heroJsonObject.value(code).toInt());
            }
            else
            {
                for(int i=0; i<(NUM_HEROS-1); i++)
                {
                    if(othersJsonObject[i].contains(code))
                    {
                        hearthArenaTiers.insert(code, othersJsonObject[i].value(code).toInt());
                        break;
                    }
                }
            }
        }
    }
    else
    {
        QJsonObject jsonCodesObject = jsonObj.value(heroString).toObject();
        const QStringList lfKeys = lightForgeTiers.keys();
        for(const QString &code: lfKeys)
        {
            if(jsonCodesObject.contains(code))
            {
                hearthArenaTiers.insert(code, jsonCodesObject.value(code).toInt());
            }
        }
    }
    emit pDebug(QStringLiteral("HearthArena Cards: %1").arg(hearthArenaTiers.count()));

    //Revision
    const QStringList lfKeys = lightForgeTiers.keys();
    for(const QString &code: lfKeys)
    {
        if(!hearthArenaTiers.contains(code))
        {
            QString haCode = getHACode(code);
            if(haCode == code)
            {
                emit pDebug(QStringLiteral("HearthArena missing: %1 - %2").arg(code, Utility::cardEnNameFromCode(code)));
            }
            else
            {
                emit pDebug(QStringLiteral("HearthArena redirect: %1 -> %2 - %3").arg(code, haCode, Utility::cardEnNameFromCode(code)));
                hearthArenaTiers.insert(code, hearthArenaTiers[haCode]);
            }
        }
    }
    emit pDebug(QStringLiteral("HearthArena Cards with doubles: %1").arg(hearthArenaTiers.count()));
}


void DraftHandler::loadCardHist(QString classUName)
{
    QString code;
    int type, rows, cols;
    bool continuous;

    QFile file(Utility::histogramsPath() + "/" + classUName + HISTOGRAM_EXT);
    if(!file.open(QIODevice::ReadOnly))
    {
        emit pDebug("ERROR: Cannot open " + file.fileName());
        return;
    }
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_5_5);

    while(!in.atEnd())
    {
        in >> code >> type >> rows >> cols >> continuous;
        cardsHist[code] = cv::Mat(rows, cols, Utility::cvTypeFromFile(type));
        cv::Mat &mat = cardsHist[code];

        if(continuous)
        {
            size_t const dataSize = rows * cols * mat.elemSize();
            in.readRawData(reinterpret_cast<char*>(mat.ptr()), dataSize);
        }
        else
        {
            size_t const rowSize(cols * mat.elemSize());
            for(int i=0; i<rows; i++)   in.readRawData(reinterpret_cast<char*>(mat.ptr(i)), rowSize);
        }
    }
    file.close();
}


void DraftHandler::saveCardHist()
{
    //Save
    const QList<CardClass> cardClassList = codesByClass.keys();
    for(const CardClass &cardClass: cardClassList)
    {
        QString classUName = Utility::classEnum2classUName(cardClass);
        QFileInfo fi(Utility::histogramsPath() + "/" + classUName + HISTOGRAM_EXT);
        if(fi.exists())
        {
            emit pDebug("Save Arena Hists SKIP (" + classUName + "): " + QString::number(codesByClass[cardClass].count()));
        }
        else
        {
            int type, rows, cols;
            bool continuous;

            QFile file(Utility::histogramsPath() + "/" + classUName + HISTOGRAM_EXT);
            if(!file.open(QIODevice::WriteOnly))
            {
                emit pDebug("ERROR: Cannot open " + file.fileName());
                return;
            }
            QDataStream out(&file);
            out.setVersion(QDataStream::Qt_5_5);

            for(QString code: (const QStringList)codesByClass[cardClass])
            {
                for(int i=0; i<2; i++)
                {
                    if(i==1)    code += "_premium";
                    if(!cardsHist.contains(code))
                    {
                        emit pDebug("WARNING: saveCardHist " + classUName + ": " + code + " not found.");
                        continue;
                    }

                    cv::Mat &mat = cardsHist[code];
                    type = Utility::cvTypeToFile(mat.type());
                    rows = mat.rows;
                    cols = mat.cols;
                    continuous = mat.isContinuous();
                    out << code << type << rows << cols << continuous;

                    if(continuous)
                    {
                        size_t const dataSize = rows * cols * mat.elemSize();
                        out.writeRawData(reinterpret_cast<char const*>(mat.ptr()), dataSize);
                    }
                    else
                    {
                        size_t const rowSize(cols * mat.elemSize());
                        for(int i=0; i<rows; i++)   out.writeRawData(reinterpret_cast<char const*>(mat.ptr(i)), rowSize);
                    }
                }
            }
            file.close();
            emit pDebug("Save Arena Hists SAVED (" + classUName + "): " + QString::number(codesByClass[cardClass].count()));
        }
    }

    needSaveCardHist = false;
}


void DraftHandler::addCardHist(QString code, bool premium)
{
    //FALSO: Evitamos golden cards de cartas no coleccionables
    // if(premium && !Utility::getCardAttribute(code, "collectible").toBool()) return;

    QString fileNameCode = premium?(code + "_premium"): code;
    //Puede ocurrir con dual class cards en multiclassArena.
    if(cardsHist.contains(fileNameCode))    return;

    QFileInfo cardFile(Utility::hscardsPath() + "/" + fileNameCode + ".png");
    if(cardFile.exists())
    {
        cv::MatND histBase = getHist(fileNameCode);
        if(!histBase.empty())   cardsHist[fileNameCode] = histBase;
    }
    //cardsDownloading no puede contener duplicados, puede ocurrir con dual class cards en multiclassArena.
    else if(!cardsDownloading.contains(fileNameCode))
    {
        //La bajamos de HearthstoneJSON/Hearthpwn
        emit checkCardImage(fileNameCode);
        cardsDownloading.append(fileNameCode);
    }
}


void DraftHandler::processCardHist(QStringList &codes)
{
    for(const QString &code: codes)
    {
        addCardHist(code, false);
        addCardHist(code, true);
    }
}


bool DraftHandler::initCardHist()
{
    bool processed = false;
    const QList<CardClass> cardClassList = codesByClass.keys();
    for(const CardClass &cardClass: cardClassList)
    {
        QString classUName = Utility::classEnum2classUName(cardClass);
        QFileInfo fi(Utility::histogramsPath() + "/" + classUName + HISTOGRAM_EXT);
        if(fi.exists())
        {
            int beforeHists = cardsHist.count();
            loadCardHist(classUName);
            emit pDebug("Load Arena Hists (" + classUName + "): " + QString::number(cardsHist.count() - beforeHists) + " hists.");
        }
        else
        {
            int beforeHists = cardsHist.count();
            processCardHist(codesByClass[cardClass]);
            emit pDebug("Process Arena Hists (" + classUName + "): " + QString::number(cardsHist.count() - beforeHists) + " hists.");
            processed = true;
        }
    }

    return processed;
}


void DraftHandler::initCardsNameMap()
{
    const QList<CardClass> cardClassList = codesByClass.keys();
    for(const CardClass &cardClass: cardClassList)
    {
        for(const QString &code: (const QStringList)codesByClass[cardClass])
        {
            QString name = Utility::cardLocalNameFromCode(code);
            name = Utility::removeAccents(name).toLower().simplified().replace(" ", "");
            cardsNameMap[name] = code;
        }
    }

    reduceCardsNameMapMulticlass();
    emit pDebug("cardNamesMap created with " + QString::number(cardsNameMap.count()) + " names.");
}


void DraftHandler::reduceCardsNameMapMulticlass()
{
    if(!multiclassArena || this->arenaHeroMulticlassPower == INVALID_CLASS) return;

    const QList<QString> nameList = cardsNameMap.keys();
    for(const QString &name: nameList)
    {
        QList<CardClass> cardClass = Utility::getClassFromCode(cardsNameMap[name]);
        if(!(cardClass.contains(NEUTRAL) || cardClass.contains(arenaHero) ||
             cardClass.contains(arenaHeroMulticlassPower)))
        {
            cardsNameMap.remove(name);
        }
    }
}


void DraftHandler::addLFCode(const QString &code, const CardClass &heroClass, const bool multiClassDraft, bool buildCodesByClass)
{
    const QList<CardClass> cardClassList = Utility::getClassFromCode(code);
    if(multiClassDraft || cardClassList.contains(NEUTRAL) || cardClassList.contains(heroClass))
    {
        lightForgeTiers.insert(code, 0);

        if(buildCodesByClass)
        {
            if(multiClassDraft)
            {
                for(const CardClass &cardClass: cardClassList)
                {
                    if(cardClass == INVALID_CLASS)  emit pDebug("WARNING: initLightForgeTiers found INVALID_CLASS: " + code);
                    else                            codesByClass[cardClass].append(code);
                }
            }
            else if(cardClassList.contains(NEUTRAL))        codesByClass[NEUTRAL].append(code);
            else/* if(cardClassList.contains(heroClass))*/  codesByClass[heroClass].append(code);
        }
    }
}


/*
 * lightForgeTiers no contiene ningun tier, solo la lista de codigos en arena
 * en un futuro podemos usarlo para una nueva tier list
 */
void DraftHandler::initLightForgeTiers(const CardClass heroClass, const bool multiClassDraft, const QStringList &arenaCodes, bool buildCodesByClass)
{
    lightForgeTiers.clear();

    //Arena sets
    for(const QString &code: arenaCodes)
    {
        addLFCode(code, heroClass, multiClassDraft, buildCodesByClass);
    }

    emit pDebug("Arena Cards: " + QString::number(lightForgeTiers.count()));
    const QList<CardClass> cardClassList = codesByClass.keys();
    for(const CardClass &cardClass: cardClassList)
    {
        QString classUName = Utility::classEnum2classUName(cardClass);
        emit pDebug("-- (" + classUName + "): " + QString::number(codesByClass[cardClass].count()));
    }
}


//Desde la reforma de arena solo cargamos los screensettings al elegir heroe y empezar draft.
//continueDraft y heroDrafts esperan a buscar el template ya que hay una pantalla pasillo.
void DraftHandler::initCodesAndHistMaps(QList<DeckCard> &deckCardList, bool skipScreenSettings)
{
    //When a draft is resumed at startup the new cards.json may not have arrived yet, and the arena pool
    //would be built without the new cards. Wait for it (max 30s, in case there is no connection).
    if(!heroDrafting && !Utility::isCardsJsonUpToDate() && cardsJsonWaits < 30)
    {
        if(cardsJsonWaits == 0)  emit pDebug("Waiting for cards.json update before building arena hists.");
        cardsJsonWaits++;
        QList<DeckCard> deckCardListCopy = deckCardList;
        QTimer::singleShot(1000, this, [=]() mutable {initCodesAndHistMaps(deckCardListCopy, skipScreenSettings);});
        return;
    }
    cardsJsonWaits = 0;

    cardsDownloading.clear();
    cardsHist.clear();
    cardsNameMap.clear();
    needSaveCardHist = false;

    if(heroDrafting)
    {
        //The heroes are read by OCR from the class labels (readHeroClasses): their random skins defeat histograms
        QTimer::singleShot(HERODRAFT_DELAY_TIME, this, [=] () {newFindScreenLoop(skipScreenSettings);});
    }
    else //if(drafting)
    {
        QTimer::singleShot(DRAFT_DELAY_TIME, this, [=] () {newFindScreenLoop(skipScreenSettings);});
        QStringList arenaCodes = Utility::getAllArenaCodes();

        //Incluimos como arenaCodes los codes del mazo actual ya que puede ocurrir que en los bundles
        //Se esten considerando otros codigos para la misma carta que no es la que HS usa.
        for(const DeckCard &deckCard: deckCardList)
        {
            const QString code = deckCard.getCode();
            if(!code.isEmpty() && !arenaCodes.contains(code))   arenaCodes << code;
        }

        initLightForgeTiers(arenaHero, multiclassArena, arenaCodes, true);
        initHearthArenaTiers(arenaHero, multiclassArena);
        needSaveCardHist = initCardHist();
        initCardsNameMap();
    }

    //Wait for cards
    if(drafting || heroDrafting)
    {
        if(cardsDownloading.isEmpty())
        {
            if(needSaveCardHist)    saveCardHist();
            newCaptureDraftLoop();
        }
        else if(!heroDrafting)
        {
            setDraftStatus(QStringLiteral("Downloading card images (%1 left)...").arg(cardsDownloading.count()));
        }
    }
}


void DraftHandler::reHistDownloadedCardImage(const QString &fileNameCode, bool missingOnWeb)
{
    if(!cardsDownloading.contains(fileNameCode)) return; //No forma parte del drafting

    if(!fileNameCode.isEmpty() && !missingOnWeb)
    {
        cv::MatND histBase = getHist(fileNameCode);
        if(!histBase.empty())   cardsHist[fileNameCode] = histBase;
    }
    cardsDownloading.removeOne(fileNameCode);
    if(!heroDrafting && !cardsDownloading.isEmpty())
        setDraftStatus(QStringLiteral("Downloading card images (%1 left)...").arg(cardsDownloading.count()));
    if(cardsDownloading.isEmpty())
    {
        if(needSaveCardHist)    saveCardHist();
        if(!heroDrafting)   setDraftStatus("Scanning cards...");
        newCaptureDraftLoop();
    }
}


void DraftHandler::clearLists(bool keepCounters)
{
    resetBundle();
    hearthArenaTiers.clear();
    lightForgeTiers.clear();
    codesByClass.clear();
    cardsHist.clear();
    cardsNameMap.clear();
    manaTemplates.clear();
    rarityTemplates.clear();
    needSaveCardHist = false;

    if(!keepCounters)//endDraft
    {
        numDraftedCards = 0;
        deckRatingHA = 0;
        deckRatingFire = 0;
    }

    for(int i=0; i<3; i++)
    {
        cardDetected[i] = false;
        draftCardMaps[i].clear();
        bestMatchesMaps[i].clear();
        ocrCodes[i] = "";
        ocrUnmatchedText[i] = "";
    }
    for(int i=0; i<5; i++)
    {
        screenRects[i] = cv::Rect(0,0,0,0);
        manaRects[i] = cv::Rect(0,0,0,0);
        rarityRects[i] = cv::Rect(0,0,0,0);
        bestCodesRedraftingReview[i] = "";
    }

    screenIndex = -1;
    screenScale = QPointF(1,1);
    numCaptured = 0;
    extendedCapture = false;
    stopLoops = true;
}


//OLD Antes manteniamos el draft y ocultabamos los overlays al salir, ya no podemos hacerlo asi ya que quiero que al elegir un legendary bundle
//el usuario salga al menu y vuelva para asi recargar el deck y recrear de cero las mecanicas y sinergias.
// void DraftHandler::enterArena()
// {
//     showOverlay();

//     if(drafting)
//     {
//         if(!screenFound())
//         {
//             QTimer::singleShot(CONTINUEDRAFT_DELAY_TIME, this, [=] () {newFindScreenLoop(true);});
//         }
//         else if(draftCards[0].getCode().isEmpty())
//         {
//             this->extendedCapture = false;
//             newCaptureDraftLoop(true);
//         }
//     }
// }


void DraftHandler::leaveArena()
{
    emit pDebug("Leave arena.");
    setDraftStatus("");
    stopLoops = true;
    stopRedraftWatch();


    if(draftScoreWindow != nullptr)        draftScoreWindow->hide();

    if(redrafting)  endRedraftReview();
    if(drafting)
    {
        redrafting = false;//endDraft en redrafting iniciara el proceso de review deck template.
        endDraft(false);
        //OLD Antes manteniamos el draft y ocultabamos los overlays al salir, ya no podemos hacerlo asi ya que quiero que al elegir un legendary bundle
        //el usuario salga al menu y vuelva para asi recargar el deck y recrear de cero las mecanicas y sinergias.
        // if(capturing)
        // {
        //     this->numCaptured = 0;

        //     //Clear guessed cards
        //     for(int i=0; i<3; i++)
        //     {
        //         cardDetected[i] = false;
        //         draftCardMaps[i].clear();
        //         bestMatchesMaps[i].clear();
        //     }
        // }
    }
    else if(heroDrafting)   endHeroDraft();

    //Debug - Save deck on leave
    // deckHandler->saveDraftDeck(Utility::classEnum2classLogNumber(arenaHero));
}


CardClass DraftHandler::findMulticlassPower(QList<DeckCard> &deckCardList)
{
    for(DeckCard &deckCard: deckCardList)
    {
        QList<CardClass> cardClassL = Utility::getClassFromCode(deckCard.getCode());
        if(cardClassL.count() != 1)  continue;
        CardClass cardClass = cardClassL.first();
        if(cardClass < NUM_HEROS && cardClass != this->arenaHero)
        {
            emit pDebug("Found MultiClassPower: " + Utility::classEnum2classUName(cardClass));
            return cardClass;
        }
    }
    emit pDebug("No MultiClassPower found");
    return INVALID_CLASS;
}


void DraftHandler::setDeckScores()
{
    if(!redrafting)  return;

    QList<DeckCard> *deckCardList = deckHandler->getDeckCardListRef();

    //Set scores
    for(DeckCard &deckCard: *deckCardList)
    {
        QString code = deckCard.getCode();
        if(code.isEmpty())    continue;
        QString fireCode = getFireCode(code);
        int scoreHA = getHAScore(code);
        float scoreFire = (fireWRMap == nullptr) ? 0 : fireWRMap[this->arenaHero][fireCode];
        int samplesFire = (fireSamplesMap == nullptr) ? 0 : fireSamplesMap[this->arenaHero][fireCode];
        deckCard.setScores(scoreHA, scoreFire, arenaHero, samplesFire);
    }

    updateRedraftRemoveList();
}


//Section 0 sorted by Firestone and section 1 by HearthArena, each empty if no deck card has its scores
void DraftHandler::updateRedraftRemoveList()
{
    clearRedraftRemoveList();
    if(!redrafting)     return;

    fillRedraftRemoveSection(0, FireStone);
    fillRedraftRemoveSection(1, HearthArena);
}


//The sections of the remove list; two copies of a card are one "name x2" entry
QList<RedraftSuggestion> DraftHandler::getRedraftRemoveSuggestions()
{
    QList<RedraftSuggestion> suggestions;
    for(int section=0; section<REDRAFT_REMOVE_SECTIONS; section++)
    {
        if(redraftRemoveCards[section].isEmpty())   continue;
        const DraftMethod draftMethod = (section == 0) ? FireStone : HearthArena;
        RedraftSuggestion suggestion;
        suggestion.source = (draftMethod == FireStone) ? "Firestone" : "HearthArena";
        for(DeckCard &deckCard: redraftRemoveCards[section])
        {
            float score = deckCard.getScore(draftMethod);
            QString scoreText = (draftMethod == HearthArena)?QString::number(qRound(score)):
                                                             QString::number(score, 'f', 1) + "%";
            QString name = deckCard.getName();
            if(deckCard.total > 1)  name += QStringLiteral(" x%1").arg(deckCard.total);
            suggestion.cards << RedraftSuggestionCard{name, scoreText, deckCard.getCode()};
        }
        suggestions << suggestion;
    }
    return suggestions;
}


void DraftHandler::fillRedraftRemoveSection(int section, DraftMethod draftMethod)
{
    QList<DeckCard> *deckCardList = deckHandler->getDeckCardListRef();
    QList<DeckCard> &removeCards = redraftRemoveCards[section];

    //One entry per copy, so both copies of a card can be suggested. stable_sort keeps the deck mana order on ties.
    QList<DeckCard *> copies;
    for(DeckCard &deckCard: *deckCardList)
    {
        if(deckCard.getCode().isEmpty() || deckCard.getScore(draftMethod) == 0)  continue;
        for(int i=0; i<deckCard.total; i++)  copies << &deckCard;
    }
    std::stable_sort(copies.begin(), copies.end(), [draftMethod](const DeckCard *a, const DeckCard *b) {
        return a->getScore(draftMethod) < b->getScore(draftMethod);
    });

    const int numCopies = std::min(static_cast<int>(copies.count()), REDRAFT_REMOVE_CARDS);
    for(int i=0; i<numCopies; i++)
    {
        if(!removeCards.isEmpty() && removeCards.last().isCode(copies[i]->getCode()))
        {
            removeCards.last().total++;
            continue;
        }
        DeckCard deckCard = *copies[i];
        deckCard.total = 1;
        removeCards << deckCard;
    }
}


void DraftHandler::clearRedraftRemoveList()
{
    for(int section=0; section<REDRAFT_REMOVE_SECTIONS; section++)  redraftRemoveCards[section].clear();
}


void DraftHandler::beginDraft(QString hero, QList<DeckCard> deckCardList, bool skipScreenSettings)
{
    if(heroDrafting)
    {
        saveTemplateSettings();
        endHeroDraft();
    }

    this->arenaHero = Utility::classLogNumber2classEnum(hero);
    if(arenaHero == INVALID_CLASS)
    {
        emit pDebug("Begin draft of unknown hero: " + hero);
        return;
    }
    else
    {
        emit pDebug("Begin draft. Hero: " + hero);
        emit pDebug(QStringLiteral("Arena Hero: %1").arg(Utility::classEnum2classUName(arenaHero)));
    }

    //Set updateTime in log / Hide card Window
    emit draftStarted();

    //Reconstruir todas las sinergias permite retocar el deck y force draft con el deck correcto.
    clearLists(false);

    if(multiclassArena) this->arenaHeroMulticlassPower = findMulticlassPower(deckCardList);
    else                this->arenaHeroMulticlassPower = INVALID_CLASS;
    this->drafting = true;
    this->justPickedCard = "";

    for(int i=0; i<3; i++)
    {
        prevCodes[i] = "";
        draftCards[i].setCode("");
    }


    initCodesAndHistMaps(deckCardList, skipScreenSettings);
    initDeckCounters(deckCardList);
    loadImgTemplates(manaTemplates, "MANA.dat");
    loadImgTemplates(rarityTemplates, "RARITY.dat");

    if(redrafting)  setDeckScores();
}


//SetDraftMode - REDRAFTING comes when a redraft is offered (the Ready Up screen with "Draft New Cards"), every time
//the arena opens until it's taken. Taking it logs nothing: its pick screen is recognized by the card names (isRedraftOffered).
void DraftHandler::redraft()
{
    this->redrafting = true;
    this->redraftPicksSeen = false;
}


void DraftHandler::checkRedraft()
{
    if(redrafting)  continueDraft();
    else            startRedraftWatch();
}


void DraftHandler::startRedraftWatch()
{
    if(redraftWatchTimer->isActive())   return;
    emit pDebug("Start watching for the redraft review screen.");
    redraftWatchTimer->start();
}


void DraftHandler::stopRedraftWatch()
{
    if(!redraftWatchTimer->isActive())  return;
    emit pDebug("Stop watching for the redraft review screen.");
    redraftWatchTimer->stop();
}


static QImage grabHearthstoneWindow(int maxWidth);
static bool isHearthstoneWindowSmall();

//Why a big number wasn't read, for the log
enum BigNumberFail {NoWindow = -1, NoAnchor = -2, NoNumber = -3};
static QString bigNumberFailText(int fail)
{
    if(fail == NoWindow)    return "no Hearthstone window";
    if(fail == NoAnchor)    return "screen labels not found";
    return "number not read";
}


//The big white number of the Hearthstone arena screens (the wins on the Ready Up medal, the rewards chest) inside
//crop, 0 to 12, or -1
static int readBigNumber(const QImage &rgb, const QRect &crop)
{
    //The white digits alone, black on white, three times in a row: Vision drops a lone digit (both recognizers,
    //depending on the digit and its size) but reads "444" or "000"; the fast one reads all of them
    QRect ink;
    for(int y=0; y<crop.height(); y++)
    {
        for(int x=0; x<crop.width(); x++)
        {
            const QRgb p = rgb.pixel(crop.x() + x, crop.y() + y);
            if(std::min({qRed(p), qGreen(p), qBlue(p)}) > 200)  ink |= QRect(crop.x() + x, crop.y() + y, 1, 1);
        }
    }
    if(ink.width() < 3 || ink.height() < 8)     return -1;
    const int gapPx = ink.height()/4;
    QImage row(gapPx + 3*(ink.width() + gapPx), ink.height()*2, QImage::Format_RGB32);
    row.fill(Qt::white);
    for(int copy=0; copy<3; copy++)
    {
        const int left = gapPx + copy*(ink.width() + gapPx);
        for(int y=0; y<ink.height(); y++)
        {
            for(int x=0; x<ink.width(); x++)
            {
                const QRgb p = rgb.pixel(ink.x() + x, ink.y() + y);
                if(std::min({qRed(p), qGreen(p), qBlue(p)}) > 200)  row.setPixel(left + x, ink.height()/2 + y, qRgb(0, 0, 0));
            }
        }
    }
    QString number;
    for(const MacOcr::TextLine &line: MacOcr::recognizeTextLines(row, "enUS", true))
    {
        for(QChar c: line.text)
        {
            //Letters the fast recognizer reads for these digits
            if(c == 'o' || c == 'O' || c == 'D')                    c = '0';
            else if(c == 'l' || c == 'I' || c == 'i' || c == '|')   c = '1';
            else if(c == 'Z' || c == 'z')                           c = '2';
            else if(c == 'S' || c == 's')                           c = '5';
            else if(c == 'B')                                       c = '8';
            if(c.isDigit())     number += c;
        }
    }
    //The three copies must agree
    if(number.isEmpty() || number.length() % 3 != 0)    return -1;
    const QString third = number.left(number.length()/3);
    if(number != third + third + third)     return -1;
    number = third;
    bool ok = false;
    const int wins = number.toInt(&ok);
    return (ok && wins >= 0 && wins <= 12) ? wins : -1;
}


//The rewards screen of a run shows its final wins on the chest, also the games the tracker didn't see (closed).
//Tried for a few seconds: the chest comes in with an animation.
void DraftHandler::readRewardsWins()
{
    rewardsWinsTries = 0;
    rewardsWinsWaits = 0;
    tryReadRewardsWins();
}


void DraftHandler::tryReadRewardsWins()
{
    if(Utility::getLocalLang() != "enUS")
    {
        emit rewardsWinsRead(-1);
        return;
    }
    if(futureRewardsWins.isRunning())   return;
    //Away from Hearthstone the tries wait for it, up to 10 minutes
    if(isHearthstoneWindowSmall() && ++rewardsWinsWaits < 600)
    {
        QTimer::singleShot(1000, this, SLOT(tryReadRewardsWins()));
        return;
    }
    rewardsWinsTries++;
    const QImage image = grabHearthstoneWindow(1400);

    futureRewardsWins.setFuture(QtConcurrent::run([image]() {
        if(image.isNull())  return int(NoWindow);
        //"Run Complete!" is the anchor: the number is under it, in its widths (measured on 2026 clients)
        QRectF label;
        for(const MacOcr::TextLine &line: MacOcr::recognizeTextLines(image, "enUS", true))
        {
            if(line.text.contains("Run Complete", Qt::CaseInsensitive))     label = line.rect;
        }
        if(label.isNull())  return int(NoAnchor);
        const qreal w = label.width();
        const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
        const QRect crop = QRect(qRound(label.center().x() - 0.22*w), qRound(label.center().y() + 0.97*w),
                                 qRound(0.44*w), qRound(0.48*w)) & rgb.rect();
        if(crop.isEmpty())  return int(NoAnchor);

        const int wins = readBigNumber(rgb, crop);
        return (wins < 0) ? int(NoNumber) : wins;
    }));
}


void DraftHandler::finishReadRewardsWins()
{
    const int wins = futureRewardsWins.result();
    if(wins >= 0)
    {
        emit pDebug("Rewards chest: " + QString::number(wins) + " wins.");
        emit rewardsWinsRead(wins);
    }
    else if(rewardsWinsTries < 8)   QTimer::singleShot(1000, this, SLOT(tryReadRewardsWins()));
    else
    {
        emit pDebug("Rewards chest not read: " + bigNumberFailText(wins) + ".");
        emit rewardsWinsRead(-1);
    }
}


//After a won game, the wins on the Ready Up medal (the real ones, also the games the tracker didn't see): tried every
//2 s for a minute, the time to come back from the game
void DraftHandler::readReadyUpWins()
{
    readyUpWinsTries = 0;
    readyUpWinsWaits = 0;
    tryReadReadyUpWins();
}


void DraftHandler::tryReadReadyUpWins()
{
    if(Utility::getLocalLang() != "enUS" || futureReadyUpWins.isRunning())  return;
    //Away from Hearthstone (e.g. another app right after the game) the tries wait for it, up to 10 minutes
    if(isHearthstoneWindowSmall() && ++readyUpWinsWaits < 300)
    {
        QTimer::singleShot(2000, this, SLOT(tryReadReadyUpWins()));
        return;
    }
    readyUpWinsTries++;
    const QImage image = grabHearthstoneWindow(1400);

    futureReadyUpWins.setFuture(QtConcurrent::run([image]() {
        if(image.isNull())  return int(NoWindow);
        //"Wins:" and "Losses:" are the anchors: the medal is under "Wins:", in their distance (measured on 2026 clients)
        //The fast recognizer mixes up i and l ("Wlns:"): compared letters only, those as one
        auto key = [](const QString &text) {
            QString letters;
            for(QChar c: text.toUpper())
            {
                if(c == 'L' || c == '1' || c == '|')    c = 'I';
                if(c.isLetter())    letters += c;
            }
            return letters;
        };
        QRectF winsLabel, lossesLabel;
        bool readyUp = false;
        for(const MacOcr::TextLine &line: MacOcr::recognizeTextLines(image, "enUS", true))
        {
            const QString text = key(line.text);
            if(text == key("Wins"))             winsLabel = line.rect;
            else if(text == key("Losses"))      lossesLabel = line.rect;
            else if(text == key("Ready Up"))    readyUp = true;
        }
        if(!readyUp || winsLabel.isNull() || lossesLabel.isNull())   return int(NoAnchor);
        const qreal gap = lossesLabel.center().x() - winsLabel.center().x();
        if(gap <= 0 || qAbs(lossesLabel.center().y() - winsLabel.center().y()) > gap*0.05)    return int(NoAnchor);

        const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
        const qreal r = 0.08*gap;
        const QPointF medal(winsLabel.center().x(), winsLabel.center().y() + 0.145*gap);
        const QRect crop = QRect(qRound(medal.x() - r), qRound(medal.y() - 0.8*r), qRound(2*r), qRound(1.6*r)) & rgb.rect();
        const int wins = crop.isEmpty() ? -1 : readBigNumber(rgb, crop);
        return (wins < 0) ? int(NoNumber) : wins;
    }));
}


void DraftHandler::finishReadReadyUpWins()
{
    const int wins = futureReadyUpWins.result();
    if(wins >= 0)
    {
        emit pDebug("Ready Up medal: " + QString::number(wins) + " wins.");
        emit readyUpWinsRead(wins);
    }
    else if(readyUpWinsTries < 30)  QTimer::singleShot(2000, this, SLOT(tryReadReadyUpWins()));
    else                            emit pDebug("Ready Up medal not read: " + bigNumberFailText(wins) + ".");
}


//Reads the deck counter in the bottom right corner of the Hearthstone window.
//Only the review screen has more than 30 cards in the deck.
void DraftHandler::checkRedraftScreen()
{
    if(drafting || heroDrafting || redrafting || arenaHero == INVALID_CLASS)  return;
    if(futureRedraftCounter.isRunning())    return;

    QRect hsRect = MacOcr::hearthstoneWindowRect();
    if(hsRect.isNull())     return;
    QRect counterRect(hsRect.x() + hsRect.width()*7/10, hsRect.y() + hsRect.height()*3/4,
                      hsRect.width()*3/10, hsRect.height()/4);

    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    if(primaryScreen == nullptr)    return;
    QImage image = primaryScreen->grabWindow(0, counterRect.x(), counterRect.y(),
                                             counterRect.width(), counterRect.height()).toImage();
    if(image.isNull())  return;
    hideTrackerWindows(image, counterRect);
    if(image.width() > 800)     image = image.scaledToWidth(800, Qt::SmoothTransformation);

    futureRedraftCounter.setFuture(QtConcurrent::run([image]() {
        //The OCR reads a red "31/30" as "31//30"
        static const QRegularExpression counterRe("(\\d{2})\\s*/+\\s*30\\b");
        const QStringList lines = MacOcr::recognizeLines(image, "");
        for(const QString &line: lines)
        {
            QRegularExpressionMatch match = counterRe.match(line);
            if(match.hasMatch())    return match.captured(1).toInt();
        }
        return 0;
    }));
}


//Marks the cards picked in the redraft review screen, in the deck and in the suggestions
void DraftHandler::setRedraftReviewCodes(const QStringList &codes)
{
    //Removed from the deck when the review ends
    for(int i=0; i<5; i++)  bestCodesRedraftingReview[i] = (i < codes.count())?codes[i]:"";
}


//Reads the Hearthstone window; the deck list on the right, from the "NN/30" counter to the right, is left out.
void DraftHandler::captureRedraftReviewNames()
{
    if(!redraftingReview)
    {
        redraftReviewTimer->stop();
        return;
    }
    if(futureRedraftReviewCodes.isRunning())    return;

    QRect hsRect = MacOcr::hearthstoneWindowRect();
    if(hsRect.isNull())     return;
    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    if(primaryScreen == nullptr)    return;
    QImage image = primaryScreen->grabWindow(0, hsRect.x(), hsRect.y(), hsRect.width(), hsRect.height()).toImage();
    if(image.isNull())  return;
    hideTrackerWindows(image, hsRect);
    if(image.width() > 1400)    image = image.scaledToWidth(1400, Qt::SmoothTransformation);

    const QMap<QString, QString> nameMap = redraftNameMap;
    const QString language = Utility::getLocalLang();
    futureRedraftReviewCodes.setFuture(QtConcurrent::run([image, nameMap, language]() {
        //The OCR reads a red "31/30" as "31//30"
        static const QRegularExpression counterRe("\\d{2}\\s*/+\\s*30\\b");
        const QList<MacOcr::TextLine> lines = MacOcr::recognizeTextLines(image, language);

        qreal deckListLeft = -1;
        for(const MacOcr::TextLine &line: lines)
        {
            if(counterRe.match(line.text).hasMatch())
            {
                deckListLeft = line.rect.center().x() - image.height()*0.1;
                break;
            }
        }
        RedraftScreenRead read;
        //Not the review screen: the "Ready Up" screen before a game (enUS title), or anything else
        if(deckListLeft < 0)
        {
            for(const MacOcr::TextLine &line: lines)
            {
                if(line.text.trimmed().compare("Ready Up", Qt::CaseInsensitive) == 0)  read.screen = RedraftScreenReadyUp;
            }
            return read;
        }

        read.screen = RedraftScreenDiscard;
        for(const MacOcr::TextLine &line: lines)
        {
            //By the center: deck list lines can start with a "NEW!" badge left of the list
            if(line.rect.center().x() >= deckListLeft)  continue;
            QString code = matchCardName({line.text}, nameMap);
            if(!code.isEmpty())     read.codes << code;
        }
        read.codes = read.codes.mid(0, 5);
        return read;
    }));
}


void DraftHandler::finishRedraftReviewNames()
{
    RedraftScreenRead result = futureRedraftReviewCodes.result();
    if(!redraftingReview)   return;
    //The OCR misses the deck counter now and then (red "31/30"): the discard screen is only gone after 3 misses
    if(result.screen == RedraftScreenOther && redraftScreen == RedraftScreenDiscard && ++redraftScreenMisses < 3)  return;
    if(result.screen != RedraftScreenOther)     redraftScreenMisses = 0;
    bool onDiscard = (result.screen == RedraftScreenDiscard);
    if(onDiscard)   redraftDiscardSeen = true;
    //After Done (queueing, Ready Up) it isn't looked for anymore
    setDraftStatus((onDiscard || redraftDiscardSeen)?"":"Looking for the discard screen...");
    if(result.screen != redraftScreen)
    {
        redraftScreen = result.screen;
        emit redraftScreenChanged(redraftScreen);
    }
    //Keep the last picks when the screen is gone (Done pressed), they are removed from the deck at the end
    if(!onDiscard)  return;

    QStringList prevCodes;
    for(int i=0; i<5; i++)  if(!bestCodesRedraftingReview[i].isEmpty())    prevCodes << bestCodesRedraftingReview[i];
    QStringList codes = result.codes;
    std::sort(prevCodes.begin(), prevCodes.end());
    std::sort(codes.begin(), codes.end());
    if(codes == prevCodes)  return;

    QStringList names;
    for(const QString &code: qAsConst(codes))   names << Utility::cardEnNameFromCode(code);
    emit pDebug("Redraft review picks: " + (names.isEmpty()?QString("none"):names.join(", ")));
    setRedraftReviewCodes(codes);
}


void DraftHandler::finishCheckRedraftScreen()
{
    int numCards = futureRedraftCounter.result();
    if(numCards <= 30 || !redraftWatchTimer->isActive())   return;
    if(drafting || heroDrafting || redrafting || arenaHero == INVALID_CLASS)  return;

    emit pDebug("Redraft review screen found: " + QString::number(numCards) + "/30 cards.");
    stopRedraftWatch();

    redrafting = true;
    initTierLists(arenaHero);
    setDeckScores();
    beginRedraftReview();
}


void DraftHandler::continueDraft()
{
    if(!drafting && arenaHero != INVALID_CLASS)
    {
        emit pDebug("Continue draft o redraft.");
        QString heroLog = Utility::classEnum2classLogNumber(arenaHero);
        beginDraft(heroLog, deckHandler->getDeckCardList(), true);
    }
    else
    {
        emit pDebug("No continue draft because already drafting or no hero.");
    }
}


//The drafted cards count and the deck's score sums start with the deck the draft continues
void DraftHandler::initDeckCounters(QList<DeckCard> &deckCardList)
{
    if(deckCardList.count() == 1 || numDraftedCards > 0)  return;

    for(DeckCard &deckCard: deckCardList)
    {
        if(deckCard.getType() == INVALID_TYPE)  continue;
        QString code = deckCard.getCode();
        for(int i=0; i<deckCard.total; i++)
        {
            numDraftedCards++;
            deckRatingHA += getHAScore(code);
            deckRatingFire += (fireWRMap == nullptr) ? 0 : fireWRMap[this->arenaHero][code];
        }
    }

    emit pDebug("Counters starts with " + QString::number(numDraftedCards) + " cards.");
}


void DraftHandler::endDraft(bool createNewArena)
{
    if(!drafting)    return;

    emit pDebug("End draft.");
    setDraftStatus("");

    //Create new arena
    //Set updateTime in log
    //The run's hero is the draft's, also when some picks were missed (the tracker started mid-draft):
    //with an empty hero no new run was created and the games went to the previous run of that hero
    int numCards = numDraftedCards;
    QString heroLog = Utility::classEnum2classLogNumber(arenaHero);
    if(numCards!=30)    emit pDebug("End draft with != 30 cards: numCards: " + QString::number(numCards));
    if(createNewArena)  emit draftEnded(heroLog);//(connect) arenaHandler->newArena() / deckHandler->saveDraftDeck()
    if(createNewArena)  emitDraftFinished();

    //Deck Score
    if(createNewArena && numCards==30)
    {
        int deckScoreHA = round(deckRatingHA/static_cast<double>(numCards));
        float deckScoreFire = round(deckRatingFire/numCards * 10)/10.0;
        emit scoreAvg(deckScoreHA, deckScoreFire, heroLog);
    }

    clearLists(false);

    this->drafting = false;
    this->justPickedCard = "";

    deleteDraftScoreWindow();

    if(redrafting)  beginRedraftReview();
}


//The deck's average scores for the mascot, over the known cards with a score (cards without data don't drag it down)
void DraftHandler::emitDraftFinished()
{
    float totalFire = 0, totalHA = 0;
    int fireCards = 0, haCards = 0, knownCards = 0;
    for(DeckCard &deckCard: *deckHandler->getDeckCardListRef())
    {
        const QString code = deckCard.getCode();
        if(code.isEmpty())  continue;
        knownCards += deckCard.total;
        const float fire = (fireWRMap == nullptr) ? 0 : fireWRMap[this->arenaHero][code];
        const float ha = getHAScore(code);
        if(fire > 0)
        {
            totalFire += fire * deckCard.total;
            fireCards += deckCard.total;
        }
        if(ha > 0)
        {
            totalHA += ha * deckCard.total;
            haCards += deckCard.total;
        }
    }
    const float avgFire = (fireCards == 0) ? 0 : totalFire/fireCards;
    const float avgHA = (haCards == 0) ? 0 : totalHA/haCards;
    emit pDebug(QStringLiteral("Deck avg for the mascot: %1 known cards - Fire %2 (%3 cards) - HA %4 (%5 cards)")
                .arg(knownCards).arg(avgFire, 0, 'f', 1).arg(fireCards).arg(avgHA, 0, 'f', 0).arg(haCards));
    emit draftFinished(knownCards, avgFire, avgHA);
}


void DraftHandler::beginRedraftReview()
{
    redraftDiscardSeen = false;
    emit pDebug("Begin redraft review.");
    setDraftStatus("Looking for the discard screen...");

    redrafting = true;
    redraftingReview = true;
    cardsDownloading.clear();

    updateRedraftRemoveList();
    cardsHist.clear();

    redraftNameMap.clear();
    for(DeckCard &deckCard: *deckHandler->getDeckCardListRef())
    {
        QString code = deckCard.getCode();
        if(code.isEmpty())    continue;
        QString name = Utility::removeAccents(Utility::cardLocalNameFromCode(code)).toLower().simplified().replace(" ", "");
        redraftNameMap[name] = code;
    }
    redraftReviewTimer->start();
    return;

    QTimer::singleShot(REDRAFT_REVIEW_DELAY_TIME, this, [=] () {newFindScreenLoop(true);});

    //Por ahora no hacemos comprobacion mana/rarity
    // loadImgTemplates(manaTemplates, "MANA.dat");
    // loadImgTemplates(rarityTemplates, "RARITY.dat");

    QStringList codes;
    for(DeckCard &deckCard: *deckHandler->getDeckCardListRef())
    {
        QString code = deckCard.getCode();
        if(code.isEmpty())    continue;
        codes << code;
    }
    processCardHist(codes);

    //Wait for cards
    if(cardsDownloading.isEmpty())  newCaptureDraftLoop();
}


void DraftHandler::heroDraftDeck(QString hero)
{
    this->arenaHero = Utility::classLogNumber2classEnum(hero);//INVALID_CLASS if empty
}


//ACTIVE_DRAFT_DECK: end of a draft, or the arena entered with a drafted deck (it's saved for the copies count)
void DraftHandler::activeDraftDeck()
{
    if(drafting)
    {
        saveTemplateSettings();
        endDraft(!redrafting);
    }
    else if(arenaHero != INVALID_CLASS && deckHandler->getDeckComplete() != nullptr)
    {
        emit saveDraftDeck(Utility::classEnum2classLogNumber(arenaHero));
    }
}


//Start game / Close app
void DraftHandler::stopDraft()
{
    stopLoops = true;
    stopRedraftWatch();


    if(redrafting)  endRedraftReview();
    if(drafting)
    {
        redrafting = false;//endDraft en redrafting iniciara el proceso de review deck template.
        endDraft(false);
    }
    else if(heroDrafting)   endHeroDraft();
}


void DraftHandler::endRedraftReview()
{
    if(redraftScreen != RedraftScreenOther)
    {
        redraftScreen = RedraftScreenOther;
        emit redraftScreenChanged(redraftScreen);
    }
    redraftScreenMisses = 0;
    //Se llama si cerramos AT, start game o leave arena.
    emit pDebug("End redraft review.");
    setDraftStatus("");
    //Debemos llamar directamente, no usar connects, ya que esto se llama desde MainWindow::leaveArena() que tambien borra el deck en DeckHandler.
    redraftReviewTimer->stop();
    if(redraftingReview)    deckHandler->redraftReviewDeck(bestCodesRedraftingReview);
    deckHandler->saveDraftDeck(Utility::classEnum2classLogNumber(arenaHero));
    clearRedraftRemoveList();
    clearLists(false);
    redrafting = false;
    redraftingReview = false;
}


void DraftHandler::closeFindScreenRects()
{
    stopLoops = true;

    if(findingFrame && futureFindScreenRects.isRunning())   futureFindScreenRects.waitForFinished();
}


void DraftHandler::deleteDraftHeroWindow()
{
    if(draftHeroWindow != nullptr)
    {
        draftHeroWindow->close();
        delete draftHeroWindow;
        draftHeroWindow = nullptr;
        emit overlayCardLeave();
    }
}


void DraftHandler::deleteDraftScoreWindow()
{
    if(draftScoreWindow != nullptr)
    {
        draftScoreWindow->close();
        delete draftScoreWindow;
        draftScoreWindow = nullptr;
        emit overlayCardLeave();
    }
}


void DraftHandler::newCaptureDraftLoop(bool delayed)
{
    stopLoops = false;

    if(!capturing && screenFound() && cardsDownloading.isEmpty() &&
        ((drafting && !lightForgeTiers.empty() && !hearthArenaTiers.empty()) || heroDrafting || redraftingReview))
    {
        capturing = true;
        if(heroDrafting)    heroCaptureClock.start();

        if(delayed)                 QTimer::singleShot(CAPTUREDRAFT_DELAY_TIME, this, SLOT(captureDraft()));
        else                        captureDraft();
    }
}


//Screen Rects detectados
void DraftHandler::captureDraft()
{
    justPickedCard = "";

    bool missingTierLists = drafting && (lightForgeTiers.empty() || hearthArenaTiers.empty());
    if((!drafting && !heroDrafting && !redraftingReview) || missingTierLists || bundlePreviewOpen ||
        stopLoops || !screenFound() || !cardsDownloading.isEmpty())
    {
        capturing = false;
        return;
    }

    if(redraftingReview)
    {
        captureDraftRedraftingReview();
    }
    else
    {
        cv::MatND screenCardsHist[3];
        //A failed screenshot (e.g. while macOS asks to allow the screen capture) is tried again: stopping here
        //left the loop dead until the arena screen was looked for again
        if(!getScreenCardsHist(screenCardsHist, 3))
        {
            if(captureFails++ == 0)     emit pDebug("Screen capture failed. Retrying...", Warning);
            //Still failing (e.g. Hearthstone's window moved and the rects are off the screen): look for the screen again
            if(captureFails >= CAPTUREDRAFT_MAX_FAILS)
            {
                emit pDebug("Screen capture failed " + QString::number(captureFails) + " times: looking for the arena screen again.");
                captureFails = 0;
                capturing = false;
                rescan();
                return;
            }
            QTimer::singleShot(CAPTUREDRAFT_RETRY_TIME, this, SLOT(captureDraft()));
            return;
        }
        if(captureFails > 0)
        {
            emit pDebug("Screen capture back after " + QString::number(captureFails) + " failures.");
            captureFails = 0;
        }
        mapBestMatchingCodes(screenCardsHist);
        const bool cardsDetected = areCardsDetected();

        //Heroes not read for a while: the screen found may be the wrong one (found while a macOS dialog covered it,
        //or before Hearthstone's window settled). Look for it again, like the mascot's Rescan.
        if(heroDrafting && !cardsDetected && heroCaptureClock.isValid() &&
            heroCaptureClock.elapsed() > HERO_CAPTURE_RESCAN_TIME)
        {
            emit pDebug("Heroes not read in " + QString::number(HERO_CAPTURE_RESCAN_TIME/1000) +
                        " s: looking for the arena screen again.");
            capturing = false;
            heroCaptureClock.invalidate();
            rescan();
            return;
        }

        if(cardsDetected)
        {
            capturing = false;
            heroCaptureClock.invalidate();
            buildBestMatchesMaps();

            if(drafting)
            {
                DraftCard bestCards[3];
                getBestCards(bestCards);
                //Only a pick leaves the bundle preview for other cards than the legendaries
                bool bundlePicked = false;
                if(bundlePending)
                {
                    bundlePicked = true;
                    for(int i=0; i<3; i++)  if(bestCards[i].getRarity() == LEGENDARY)  bundlePicked = false;
                }
                showNewCards(bestCards);
                if(bundlePicked)    confirmBundle();
                startReviewBestCards();
            }
            else if(heroDrafting)
            {
                if(isRepeatHero())  capturing = true;
                else                showNewHeroes();
            }
        }

        if(capturing)
        {
            if(numCaptured == 0)    QTimer::singleShot(CAPTUREDRAFT_LOOP_TIME_FADING, this, SLOT(captureDraft()));
            else                    QTimer::singleShot(CAPTUREDRAFT_LOOP_TIME, this, SLOT(captureDraft()));
        }
    }
}


void DraftHandler::captureDraftRedraftingReview()
{
    cv::MatND screenCardsHist[5];
    if(!getScreenCardsHist(screenCardsHist, 5))
    {
        capturing = false;
        return;
    }

    QStringList reviewCodes;
    for(int i=0; i<5; i++)  reviewCodes << bestCodesRedraftingReview[i];

    double bestMatches[5];
    for(int i=0; i<5; i++)
    {
        bestMatches[i] = CARD_ACCEPTED_THRESHOLD_REDRAFT;

        for(QMap<QString, cv::MatND>::const_iterator it=cardsHist.constBegin(); it!=cardsHist.constEnd(); it++)
        {
            QString code = it.key();
            double match = compareHist(screenCardsHist[i], it.value(), 3);
            if(match < bestMatches[i])
            {
                bestMatches[i] = match;
                reviewCodes[i] = degoldCode(code);
            }
        }
    }

    setRedraftReviewCodes(reviewCodes);

    // qDebug()<<cardsHist.keys();
    // qDebug()<<endl;
    // for(int i=0; i<5; i++)
    // {
    //     qDebug()<<"BEST: "<<bestCodesRedraftingReview[i]<<bestMatches[i];
    // }

    QTimer::singleShot(CAPTUREDRAFT_LOOP_TIME_REDRAFT, this, SLOT(captureDraft()));
}


bool DraftHandler::isRepeatHero()
{
    QString heroClass[3];
    for(int i=0; i<3; i++)
    {
        if(bestMatchesMaps[i].isEmpty())    return true;
        QString code = bestMatchesMaps[i].first();
        heroClass[i] = Utility::getCardAttribute(code, "cardClass").toString();
    }

    if(heroClass[0]==heroClass[1] || heroClass[0]==heroClass[2] || heroClass[1]==heroClass[2])
    {
        emit pDebug("Skip repeated hero draft: " + heroClass[0] + " - " + heroClass[1] + " - " + heroClass[2]);
        for(int i=0; i<3; i++)
        {
            cardDetected[i] = false;
            draftCardMaps[i].clear();
            bestMatchesMaps[i].clear();
            ocrCodes[i] = "";
            ocrUnmatchedText[i] = "";
        }
        numCaptured = 0;
        return true;
    }
    return false;
}


bool DraftHandler::areCardsDetected()
{
    //On the Ready Up screen the frame search can match too and the histograms "find" cards in the medal and the chest:
    //the redraft's picks need real card names
    if(isRedraftOffered())
    {
        int names = 0;
        for(int i=0; i<3; i++)  if(!ocrCodes[i].isEmpty())  names++;
        if(names < 2)   return false;
        redraftPicksSeen = true;
        emit pDebug("Redraft pick screen: " + QString::number(names) + " card names read.");
    }

    for(int i=0; i<3; i++)
    {
        if(!cardDetected[i] && !ocrCodes[i].isEmpty())  cardDetected[i] = true;
        if(!cardDetected[i] && (numCaptured > 2) &&
            (getMinMatch(draftCardMaps[i]) < (CARD_ACCEPTED_THRESHOLD + numCaptured*CARD_ACCEPTED_THRESHOLD_INCREASE)))
        {
            cardDetected[i] = true;
        }
    }

    return (cardDetected[0] && cardDetected[1] && cardDetected[2]);
}


double DraftHandler::getMinMatch(const QMap<QString, DraftCard> &draftCardMaps)
{
    double minMatch = 1;
    QList<DraftCard> cardList = draftCardMaps.values();
    for(DraftCard &card: cardList)
    {
        double match = card.getBestQualityMatches();
        if(match < minMatch)    minMatch = match;
    }
    return minMatch;
}


void DraftHandler::buildBestMatchesMaps()
{
    if(drafting)
    {
        QStringList slotCodes[3];   //Candidates of each slot sorted by match, each card once (golden or not)
        for(int i=0; i<3; i++)
        {
            QMultiMap<double, QString> bestMatchesDups;
            const QList<QString> codeList = draftCardMaps[i].keys();
            for(const QString &code: codeList)
            {
                double match = draftCardMaps[i][code].getBestQualityMatches();
                bestMatchesDups.insert(match, code);
            }

            QStringList insertedCodes;
            const QList<QString> codeListBest = bestMatchesDups.values();
            for(const QString &code: codeListBest)
            {
                if(!insertedCodes.contains(degoldCode(code)))
                {
                    slotCodes[i].append(code);
                    insertedCodes.append(degoldCode(code));
                }
            }
        }

        applyOcrCodes(slotCodes);
        removeDuplicatedPicks(slotCodes);

        for(int i=0; i<3; i++)
        {
            for(const QString &code: qAsConst(slotCodes[i]))
            {
                double match = draftCardMaps[i][code].getBestQualityMatches();
                bestMatchesMaps[i].insert(match, code);
            }
        }
    }
    else if(heroDrafting)
    {
        for(int i=0; i<3; i++)
        {
            //Class read from the slot label: that hero alone
            if(!ocrCodes[i].isEmpty())
            {
                if(!draftCardMaps[i].contains(ocrCodes[i]))  draftCardMaps[i].insert(ocrCodes[i], DraftCard(ocrCodes[i], false));
                draftCardMaps[i][ocrCodes[i]].setBestQualityMatch(0, true);
                bestMatchesMaps[i].insert(0, ocrCodes[i]);
                continue;
            }

            const QList<QString> codeList = draftCardMaps[i].keys();
            for(const QString &code: codeList)
            {
                double match = draftCardMaps[i][code].getBestQualityMatches();
                bestMatchesMaps[i].insert(match, code);
            }
        }
    }
}

//The 3 options of a pick are always different cards. If two slots have the same best card
//(e.g. a misread animated golden card), the slot with the worse match moves to its next candidate.
void DraftHandler::removeDuplicatedPicks(QStringList slotCodes[3])
{
    for(int iteration=0; iteration<3; iteration++)
    {
        bool changed = false;
        for(int i=0; i<3; i++)
        {
            for(int j=i+1; j<3; j++)
            {
                if(slotCodes[i].isEmpty() || slotCodes[j].isEmpty())                        continue;
                if(degoldCode(slotCodes[i].first()) != degoldCode(slotCodes[j].first()))   continue;

                double matchI = draftCardMaps[i][slotCodes[i].first()].getBestQualityMatches();
                double matchJ = draftCardMaps[j][slotCodes[j].first()].getBestQualityMatches();
                int loser = (matchI > matchJ)?i:j;
                if(slotCodes[loser].count() < 2)    continue;

                emit pDebug("Duplicate pick " + degoldCode(slotCodes[loser].first()) +
                            " in slots " + QString::number(i+1) + "/" + QString::number(j+1) +
                            ", slot " + QString::number(loser+1) + " uses " + slotCodes[loser].at(1));
                slotCodes[loser].removeFirst();
                changed = true;
            }
        }
        if(!changed)    break;
    }
}


//Reads each card's name from its banner (below the art). The name doesn't change on animated golden cards,
//so it's more reliable than the art histogram. macOS only (Apple Vision).
void DraftHandler::readCardNames(const cv::Mat &screenCapture)
{
    if(cardsNameMap.isEmpty())  return;

    //For a few seconds after a pick the previous 3 cards can still be on screen
    bool recentPick = ((QDateTime::currentSecsSinceEpoch() - prevCodesTime) < PREV_CODES_TIME);

    //The first pick is the legendary groups: curved names, lower on the card, read in parts. Only legendaries.
    const bool legendaryPick = isEmptyDeck() && !redrafting;
    QMap<QString, QString> legendaryNameMap;
    if(legendaryPick)
    {
        for(auto it=cardsNameMap.constBegin(); it!=cardsNameMap.constEnd(); it++)
            if(Utility::getRarityFromCode(it.value()) == LEGENDARY)     legendaryNameMap[it.key()] = it.value();
    }

    for(int i=0; i<3; i++)
    {
        if(!ocrCodes[i].isEmpty())  continue;

        //Name banner, measured on arenaTemplate.png relative to the art rect
        const cv::Rect &art = screenRects[i];
        cv::Rect banner;
        if(legendaryPick)   banner = cv::Rect(art.x - art.width*0.65, art.y + art.height*1.08, art.width*2.4, art.height*0.7);
        else
        {
            //Centered on the card (0.15 art widths left of the art's center), 1.25 art widths each side: long names
            //("Spirit of the Kaldorei") lost their start with the old crop, which reached 1.4 right but 1 left.
            //The found arts can be spaced a bit short of the cards: plateScale (from the names read) moves the crop.
            //0.62 art heights down: spell and signature card names sit lower than the minions' scroll and 0.44
            //cut them ("Dethrone" read as "nwAt"); the card text still starts below.
            const double scale = (plateScale > 0) ? plateScale : 1.0;
            const double artSpacing = screenRects[1].x - screenRects[0].x;
            const double center = art.x + art.width*0.35 + i*artSpacing*(scale - 1);
            const double halfWidth = art.width*1.25*scale;
            banner = cv::Rect(center - halfWidth, art.y + art.height*1.08, halfWidth*2, art.height*0.62*scale);
        }
        banner &= cv::Rect(0, 0, screenCapture.cols, screenCapture.rows);
        if(banner.width < 10 || banner.height < 5)  continue;

        cv::Mat crop = screenCapture(banner).clone();
        QImage image(crop.data, crop.cols, crop.rows, static_cast<qsizetype>(crop.step), QImage::Format_RGB32);
        const QList<MacOcr::TextLine> textLines = MacOcr::recognizeTextLines(image.copy(), Utility::getLocalLang());
        QStringList lines;
        for(const MacOcr::TextLine &line: textLines)    lines << line.text;
        QString code = legendaryPick ? matchCardName(lines, legendaryNameMap, true) : matchCardName(lines, cardsNameMap);
        //The first trio after a group's pick still finds the deck empty (the group counts on this trio): normal cards
        if(code.isEmpty() && legendaryPick)     code = matchCardName(lines, cardsNameMap);
        if(code.isEmpty())
        {
            //Log each different unmatched reading once, to find out why a banner isn't recognized
            const QString text = lines.join(" ");
            if(!text.isEmpty() && text != ocrUnmatchedText[i])
            {
                ocrUnmatchedText[i] = text;
                emit pDebug("OCR slot " + QString::number(i+1) + " unmatched: \"" + text + "\"");
            }
            continue;
        }
        if(recentPick && code == prevCodes[i])  continue;

        ocrCodes[i] = code;
        emit pDebug("OCR slot " + QString::number(i+1) + ": \"" + lines.join(" ") + "\" --> " +
                    code + " " + Utility::cardEnNameFromCode(code));

        //Where the name is: the plates go right under the card. The banner can also show a neighbour's
        //name, so the matching line closest to the banner's center. Not on the legendary groups (curved names),
        //but yes on the first trio after a group's pick, still read as a legendary pick.
        ocrNameCodes[i] = "";
        if(legendaryPick && Utility::getRarityFromCode(code) == LEGENDARY)  continue;
        double bestDist = -1;
        for(const MacOcr::TextLine &line: textLines)
        {
            if(matchCardName({line.text}, cardsNameMap) != code)    continue;
            const double dist = std::abs(line.rect.center().x() - banner.width/2.0);
            if(bestDist >= 0 && dist >= bestDist)  continue;
            bestDist = dist;
            ocrNameCodes[i] = code;
            ocrNameCenters[i] = QPointF(banner.x + line.rect.center().x(), banner.y + line.rect.center().y());
        }
    }
}


//Reads the class label under each hero (enUS only). Hero skins make the portrait histograms unreliable.
void DraftHandler::readHeroClasses(const cv::Mat &screenCapture)
{
    if(Utility::getLocalLang() != "enUS")   return;

    static const QStringList classNames = {"DEATHKNIGHT", "DEMONHUNTER", "DRUID", "HUNTER", "MAGE", "PALADIN",
                                           "PRIEST", "ROGUE", "SHAMAN", "WARLOCK", "WARRIOR"};

    for(int i=0; i<3; i++)
    {
        if(!ocrCodes[i].isEmpty())  continue;

        //Lower part of the portrait slot and the label below it
        const cv::Rect &slot = screenRects[i];
        cv::Rect label(slot.x - slot.width*0.3, slot.y + slot.height*0.5, slot.width*1.6, slot.height*0.9);
        label &= cv::Rect(0, 0, screenCapture.cols, screenCapture.rows);
        if(label.width < 10 || label.height < 5)    continue;

        cv::Mat crop = screenCapture(label).clone();
        QImage image(crop.data, crop.cols, crop.rows, static_cast<qsizetype>(crop.step), QImage::Format_RGB32);
        const QStringList lines = MacOcr::recognizeLines(image.copy(), "enUS");

        //A label can be split in two lines (DEATH / KNIGHT)
        QStringList texts;
        for(int j=0; j<lines.count(); j++)
        {
            QString text;
            for(const QChar &c: lines[j].toUpper())     if(c.isLetter())    text += c;
            texts << text;
            if(j > 0)   texts << texts[texts.count()-2] + text;
        }

        QString heroClass;
        for(const QString &className: classNames)
        {
            if(texts.contains(className))
            {
                heroClass = className;
                break;
            }
        }
        if(heroClass.isEmpty())     continue;

        for(const QString &code: qAsConst(heroCodesList))
        {
            if(Utility::getCardAttribute(code, "cardClass").toString() == heroClass)
            {
                ocrCodes[i] = code;
                emit pDebug("OCR hero slot " + QString::number(i+1) + ": \"" + lines.join(" ") + "\" --> " + heroClass + " " + code);
                break;
            }
        }
    }
}


//The Hearthstone window, scaled down to maxWidth
static QImage grabHearthstoneWindow(int maxWidth)
{
    QRect hsRect = MacOcr::hearthstoneWindowRect();
    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    if(hsRect.isNull() || primaryScreen == nullptr)     return QImage();
    QImage image = primaryScreen->grabWindow(0, hsRect.x(), hsRect.y(), hsRect.width(), hsRect.height()).toImage();
    hideTrackerWindows(image, hsRect);
    if(image.width() > maxWidth)    image = image.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    return image;
}


//Hearthstone is minimized, hidden or only a Stage Manager thumbnail: nothing on it can be read
static bool isHearthstoneWindowSmall()
{
    return MacOcr::hearthstoneWindowRect().width() < 600;
}


//Card name of an OCR line without the mana cost before it or the copies count after it
static QString trimCardLine(const QString &line)
{
    int start = 0, end = line.length();
    while(start < end && !line[start].isLetter())   start++;
    while(end > start && !line[end-1].isLetter())   end--;
    return line.mid(start, end - start);
}


void DraftHandler::resetBundle()
{
    bundleTimer->stop();
    bundlePending = bundlePreviewVisible = bundlePreviewOpen = false;
    bundleMisses = 0;
    bundleLegendary = "";
    bundlePreviews.clear();
    bundleNameMap.clear();
    bundleReads = 0;
}


void DraftHandler::buildBundleNameMap()
{
    if(!bundleNameMap.isEmpty())    return;

    for(const QString &code: (const QStringList)Utility::getWildCodes())
    {
        if(Utility::getTypeFromCode(code) == HERO)  continue;
        const QList<CardClass> cardClass = Utility::getClassFromCode(code);
        if(!cardClass.contains(NEUTRAL) && !cardClass.contains(arenaHero) &&
            !(arenaHeroMulticlassPower != INVALID_CLASS && cardClass.contains(arenaHeroMulticlassPower)))   continue;
        QString name = Utility::removeAccents(Utility::cardLocalNameFromCode(code)).toLower().simplified().replace(" ", "");
        bundleNameMap[name] = code;
    }
    //Same name: the arena code
    for(QMap<QString, QString>::const_iterator it=cardsNameMap.constBegin(); it!=cardsNameMap.constEnd(); it++)
    {
        bundleNameMap[it.key()] = it.value();
    }
    emit pDebug("Bundle names map: " + QString::number(bundleNameMap.count()) + " cards.");
}


//A status line message that goes away by itself
void DraftHandler::showDraftNotice(const QString &text)
{
    setDraftStatus(text);
    QTimer::singleShot(5000, this, [this, text]() {
        if(draftStatus == text)     setDraftStatus("");
    });
}


void DraftHandler::startBundlePreview(const QString &code)
{
    //The pick of the bundle itself can log its legendary again
    for(DeckCard &deckCard: *deckHandler->getDeckCardListRef())
    {
        if(deckCard.getCode() == code)  return;
    }

    emit pDebug("Bundle preview: " + code + " " + Utility::cardEnNameFromCode(code));
    bundleLegendary = code;
    bundlePending = true;
    bundlePreviewOpen = true;
    bundlePreviewVisible = false;
    bundleMisses = 0;
    bundleReads = 0;
    buildBundleNameMap();
    if(draftScoreWindow != nullptr)    draftScoreWindow->hideScores();
    setDraftStatus("Analyzing bundle...");
    bundleTimer->start();
}


void DraftHandler::captureBundlePreview()
{
    if(!bundlePending || !drafting)
    {
        bundleTimer->stop();
        return;
    }
    if(futureBundle.isRunning())    return;

    QImage image = grabHearthstoneWindow(1400);
    if(image.isNull())  return;

    const QMap<QString, QString> nameMap = bundleNameMap;
    const QString legendary = bundleLegendary;
    const QString language = Utility::getLocalLang();
    futureBundle.setFuture(QtConcurrent::run([image, nameMap, legendary, language]() {
        QList<QPair<QString, QRectF>> cards;
        for(const MacOcr::TextLine &line: MacOcr::recognizeTextLines(image, language))
        {
            //The deck list, right, is not part of the preview; the window title "Hearthstone" is also a card
            if(line.rect.center().x() > image.width()*0.75)     continue;
            if(line.rect.center().y() < image.height()*0.08)    continue;
            QString code = matchCardName({trimCardLine(line.text)}, nameMap);
            if(!code.isEmpty())     cards << qMakePair(code, line.rect);
        }

        //The preview shows the legendary big, with its bundle cards listed up and to its right, and the
        //other legendaries blurred behind. In the choice of legendaries their names are side by side.
        QRectF legendaryRect;
        for(const auto &card: qAsConst(cards))
        {
            if(card.first == legendary)     legendaryRect = card.second;
            else if(Utility::getRarityFromCode(card.first) == LEGENDARY)    return qMakePair(false, QStringList());
        }
        if(legendaryRect.isNull())  return qMakePair(false, QStringList());

        QStringList codes;
        for(const auto &card: qAsConst(cards))
        {
            const QRectF &rect = card.second;
            if(card.first == legendary || codes.contains(card.first))  continue;
            if(rect.center().x() < legendaryRect.center().x() + image.width()*0.1)    continue;
            if(rect.center().y() > legendaryRect.center().y())  continue;
            codes << card.first;
        }
        return qMakePair(true, codes.mid(0, 3));
    }));
}


void DraftHandler::finishBundlePreview()
{
    QPair<bool, QStringList> result = futureBundle.result();
    if(!bundlePending)  return;

    if(result.first)
    {
        bundlePreviewVisible = true;
        bundleMisses = 0;
        QStringList &codes = bundlePreviews[bundleLegendary];
        if(result.second.count() > codes.count())
        {
            codes = result.second;
            QStringList names;
            for(const QString &code: qAsConst(codes))   names << Utility::cardEnNameFromCode(code);
            emit pDebug("Bundle of " + Utility::cardEnNameFromCode(bundleLegendary) + ": " + names.join(", "));
        }
        bundleReads++;
        if(codes.count() >= 3)      setDraftStatus("Bundle read: 3 cards");
        else if(bundleReads >= 5)   setDraftStatus("Can't read this bundle, the deck list will be read after the pick");
        else                        setDraftStatus("Analyzing bundle...");
    }
    //Preview closed, or not seen after its log line: picked, or back to the legendaries. The next cards tell;
    //the timer keeps watching for another preview until the pick.
    else if(bundlePreviewVisible || ++bundleMisses == 3)
    {
        bundlePreviewVisible = false;
        bundlePreviewOpen = false;
        setDraftStatus("Scanning the next cards...");
        //Forget the legendaries, like a pick does: their names were kept and the name reading skips read slots,
        //so the next cards were never read
        for(int i=0; i<3; i++)
        {
            cardDetected[i] = false;
            draftCardMaps[i].clear();
            bestMatchesMaps[i].clear();
            ocrCodes[i] = "";
            ocrUnmatchedText[i] = "";
        }
        numCaptured = 0;
        newCaptureDraftLoop();
    }
}


void DraftHandler::confirmBundle()
{
    bundlePending = bundlePreviewOpen = false;
    bundleTimer->stop();
    const QString legendary = bundleLegendary;
    const QStringList codes = bundlePreviews.value(legendary);
    bundlePreviews.clear();

    emit pDebug("Bundle picked: " + legendary + " + " + codes.join(" "));
    //The screen was found on the legendaries, bigger and higher than the cards of a trio: the plates of this first
    //trio sat too high, over the cards
    checkScreenAgain("after the legendary group");
    emit newDeckCard(legendary);
    for(const QString &code: codes)     emit newDeckCard(code);

    if(codes.count() >= 3)
    {
        showDraftNotice("Bundle added: " + Utility::cardLocalNameFromCode(legendary) + " + 3 cards");
    }
    else
    {
        //Give the deck list time to show the new cards
        setDraftStatus("Scanning the deck list...");
        QTimer::singleShot(1500, this, [this]() {readDeckList();});
    }
}


//Looks for the arena screen once more, soon: a changed layout is "Not the same" and places the plates again
void DraftHandler::checkScreenAgain(const QString &reason)
{
    QTimer::singleShot(FINDSCREEN_VERIFY_TIME, this, [this, reason]() {
        if(findingFrame || stopLoops || screenIndex == -1)  return;
        emit pDebug("Checking the arena screen again " + reason + ".");
        findingFrame = true;
        startFindScreenRects();
    });
}


//Adds the deck list cards (right of the draft screen) the tracker doesn't have, at most a bundle
void DraftHandler::readDeckList()
{
    if(!drafting || futureDeckList.isRunning())     return;

    QImage image = grabHearthstoneWindow(1400);
    if(image.isNull())  return;

    buildBundleNameMap();
    const QMap<QString, QString> nameMap = bundleNameMap;
    const QString language = Utility::getLocalLang();
    futureDeckList.setFuture(QtConcurrent::run([image, nameMap, language]() {
        QStringList codes;
        for(const MacOcr::TextLine &line: MacOcr::recognizeTextLines(image, language))
        {
            if(line.rect.center().x() < image.width()*0.75)     continue;
            if(line.rect.center().y() < image.height()*0.08)    continue;
            QString code = matchCardName({trimCardLine(line.text)}, nameMap);
            if(!code.isEmpty() && !codes.contains(code))    codes << code;
        }
        return codes;
    }));
}


void DraftHandler::finishDeckList()
{
    QStringList listCodes = futureDeckList.result();
    if(!drafting)   return;

    QStringList deckNames;
    for(DeckCard &deckCard: *deckHandler->getDeckCardListRef())
    {
        if(!deckCard.getCode().isEmpty())   deckNames << Utility::cardEnNameFromCode(deckCard.getCode());
    }

    QStringList added;
    for(const QString &code: qAsConst(listCodes))
    {
        if(added.count() >= 3)  break;
        if(deckNames.contains(Utility::cardEnNameFromCode(code)))   continue;
        emit newDeckCard(code);
        added << Utility::cardEnNameFromCode(code);
    }

    emit pDebug("Deck list read: +" + QString::number(added.count()) + " " + added.join(", "));
    if(added.isEmpty())     showDraftNotice("Bundle cards not found in the deck list");
    else                    showDraftNotice("Deck list read: +" + QString::number(added.count()) + " cards");
}


//Finds the name of nameMap (normalized name -> code) closest to the OCR text.
QString DraftHandler::matchCardName(const QStringList &lines, const QMap<QString, QString> &nameMap, bool partial)
{
    auto normalize = [](const QString &text) {
        QString norm;
        for(const QChar &c: Utility::removeAccents(text).toLower())
        {
            if(c.isLetterOrNumber())    norm += c;
        }
        return norm;
    };
    //Similarity 0..1 based on Levenshtein distance
    auto similarity = [](const QString &a, const QString &b) {
        if(a.isEmpty() || b.isEmpty())  return 0.0;
        QVector<int> prev(b.length()+1), cur(b.length()+1);
        for(int j=0; j<=b.length(); j++)    prev[j] = j;
        for(int i=1; i<=a.length(); i++)
        {
            cur[0] = i;
            for(int j=1; j<=b.length(); j++)
            {
                int cost = (a[i-1] == b[j-1])?0:1;
                cur[j] = std::min({prev[j]+1, cur[j-1]+1, prev[j-1]+cost});
            }
            std::swap(prev, cur);
        }
        return 1.0 - prev[b.length()]/static_cast<double>(std::max(a.length(), b.length()));
    };
    //Similarity 0..1 of text with its closest part of name (Levenshtein, free start and end in name)
    auto partSimilarity = [](const QString &text, const QString &name) {
        if(text.length() < 5 || name.isEmpty())     return 0.0;
        QVector<int> prev(name.length()+1, 0), cur(name.length()+1);
        for(int i=1; i<=text.length(); i++)
        {
            cur[0] = i;
            for(int j=1; j<=name.length(); j++)
            {
                int cost = (text[i-1] == name[j-1])?0:1;
                cur[j] = std::min({prev[j]+1, cur[j-1]+1, prev[j-1]+cost});
            }
            std::swap(prev, cur);
        }
        return 1.0 - *std::min_element(prev.begin(), prev.end())/static_cast<double>(text.length());
    };

    QStringList texts;
    for(const QString &line: lines)     texts << normalize(line);
    if(lines.count() > 1)               texts << normalize(lines.join(""));

    double best = 0, second = 0;
    QString bestCode;
    for(QMap<QString, QString>::const_iterator it=nameMap.constBegin(); it!=nameMap.constEnd(); it++)
    {
        const QString name = normalize(it.key());
        double sim = 0;
        for(const QString &text: qAsConst(texts))
        {
            sim = std::max(sim, similarity(text, name));
            if(partial)     sim = std::max(sim, partSimilarity(text, name));
            //Name cut by a banner edge ("Holy Eggbea", "cover Cultist"): compare with the start
            //or the end of the name if at least 60% of it was read.
            if(text.length() < name.length() && text.length() >= 0.6*name.length())
            {
                sim = std::max(sim, similarity(text, name.left(text.length())));
                sim = std::max(sim, similarity(text, name.right(text.length())));
            }
        }

        if(sim > best)
        {
            if(it.value() != bestCode)  second = best;
            best = sim;
            bestCode = it.value();
        }
        else if(sim > second && it.value() != bestCode)
        {
            second = sim;
        }
    }

    //Accept only a close match with no other card almost as close
    if(best >= (partial ? 0.7 : 0.8) && (best - second) >= 0.1)   return bestCode;
    //Or a looser match far ahead of every other card: the bent names are read with errors at their ends
    //("San of the Kaldores" 0.75 against 0.44 of the next card). Readings of other text stay below 0.6.
    if(!partial && best >= 0.65 && (best - second) >= 0.2)   return bestCode;
    return "";
}


//Slots whose name was read by OCR use that card as the best one, over the histogram.
void DraftHandler::applyOcrCodes(QStringList slotCodes[3])
{
    for(int i=0; i<3; i++)
    {
        if(ocrCodes[i].isEmpty())   continue;

        //If the histogram already had it (plain or golden) use that version
        QString chosen;
        for(const QString &code: qAsConst(slotCodes[i]))
        {
            if(degoldCode(code) == ocrCodes[i])
            {
                chosen = code;
                break;
            }
        }
        if(chosen.isEmpty())
        {
            chosen = ocrCodes[i];
            if(!draftCardMaps[i].contains(chosen))  draftCardMaps[i].insert(chosen, DraftCard(chosen, false));
        }
        draftCardMaps[i][chosen].setBestQualityMatch(0, true);
        slotCodes[i].removeAll(chosen);
        slotCodes[i].prepend(chosen);
    }
}


void DraftHandler::getBestCards(DraftCard bestCards[3])
{
    for(int i=0; i<3; i++)
    {
        double match = bestMatchesMaps[i].keys().first();
        QString code = bestMatchesMaps[i].values().first();
        QString name = draftCardMaps[i][code].getName();
        QString cardInfo = code + " " + name + " " + QString::number(static_cast<int>(match*1000)/1000.0);

        bestCards[i] = draftCardMaps[i][code];
        emit pDebug("Choose: " + cardInfo);
    }

    emit pDebug("(" + QString::number(numDraftedCards) + ") " +
                bestCards[0].getCode() + "/" + bestCards[1].getCode() +
                "/" + bestCards[2].getCode() + " New codes.");
}


void DraftHandler::pickCard(QString code)
{
    if(!drafting)
    {
        emit pDebug("Pick hero: " + code);
        return;
    }
    if(justPickedCard==code)
    {
        emit pDebug("WARNING: Duplicate pick code detected: " + code);
        return;
    }
    if(!redrafting && Utility::getRarityFromCode(code) == LEGENDARY)
    {
        startBundlePreview(code);
        return;
    }
    //Saltamos legendary bundles
    if(!redrafting && Utility::getRarityFromCode(code) == LEGENDARY)
    {
        emit pDebug("Skip pick legendary: " + code);
        if(draftScoreWindow != nullptr)
        {
            draftScoreWindow->hideScores();
            draftScoreWindow->showMinimized();
        }
        return;
    }

    //Completa 2nd class en mutiClassArena (pick hero power)
    CardType cardType = Utility::getTypeFromCode(code);
    if(cardType == HERO_POWER)
    {
        emit pDebug("Pick hero power: " + code);
        QList<CardClass> cardClassL = Utility::getClassFromCode(code);
        CardClass cardClass = cardClassL.first();
        if(cardClass < NUM_HEROS && cardClass != this->arenaHero)
        {
            this->arenaHeroMulticlassPower = cardClass;
            reduceCardsNameMapMulticlass();
            emit pDebug("Found MultiClassPower: " + Utility::classEnum2classUName(arenaHeroMulticlassPower));
        }
        return;
    }

    bool delayCapture = true;
    if(code=="0" || code=="1" || code=="2")
    {
        code = draftCards[code.toInt()].getCode();
        delayCapture = false;
    }

    //Completa 2nd class en mutiClassArena (pick class card)
    if(multiclassArena && arenaHeroMulticlassPower==INVALID_CLASS)
    {
        QList<CardClass> cardClassL = Utility::getClassFromCode(code);
        if(cardClassL.count() == 1)
        {
            CardClass cardClass = cardClassL.first();
            if(cardClass < NUM_HEROS && cardClass != this->arenaHero)
            {
                this->arenaHeroMulticlassPower = cardClass;
                reduceCardsNameMapMulticlass();
                emit pDebug("Found MultiClassPower: " + Utility::classEnum2classUName(arenaHeroMulticlassPower));
            }
        }
    }

    //The drafted cards count (the run's hero, the legendary groups pick) and the deck average
    numDraftedCards++;
    updateDeckScore(getHAScore(code), (fireWRMap == nullptr) ? 0 : fireWRMap[this->arenaHero][code]);

    //Clear cards and score
    for(int i=0; i<3; i++)
    {
        prevCodes[i] = draftCards[i].getCode();
        draftCards[i].setCode("");
        cardDetected[i] = false;
        draftCardMaps[i].clear();
        bestMatchesMaps[i].clear();
        ocrCodes[i] = "";
        ocrUnmatchedText[i] = "";
    }

    prevCodesTime = QDateTime::currentSecsSinceEpoch();
    this->numCaptured = 0;
    this->extendedCapture = false;
    if(draftScoreWindow != nullptr)    draftScoreWindow->hideScores();

    emit pDebug("Pick card: " + code);
    emit newDeckCard(code);

    this->justPickedCard = code;

    setDraftStatus("Scanning the next cards...");
    newCaptureDraftLoop(delayCapture);
}


void DraftHandler::refreshCapturedCards()
{
    if(!drafting)   return;

    //Clear cards and score
    for(int i=0; i<3; i++)
    {
        prevCodes[i] = "";
        draftCards[i].setCode("");
        cardDetected[i] = false;
        draftCardMaps[i].clear();
        bestMatchesMaps[i].clear();
        ocrCodes[i] = "";
        ocrUnmatchedText[i] = "";
    }

    this->numCaptured = 0;
    this->extendedCapture = true;
    if(draftScoreWindow != nullptr)    draftScoreWindow->hideScores();

    newCaptureDraftLoop();
}


void DraftHandler::refreshDraft()
{
    if(!drafting)   return;

    emit pDebug("\nRefresh Draft.");

    //Clear cards and score
    for(int i=0; i<3; i++)
    {
        prevCodes[i] = "";
        draftCards[i].setCode("");
        cardDetected[i] = false;
        draftCardMaps[i].clear();
        bestMatchesMaps[i].clear();
        ocrCodes[i] = "";
        ocrUnmatchedText[i] = "";

        screenRects[i] = cv::Rect(0,0,0,0);
        manaRects[i] = cv::Rect(0,0,0,0);
        rarityRects[i] = cv::Rect(0,0,0,0);
        bestCodesRedraftingReview[i] = "";
    }

    numDraftedCards = 0;
    deckRatingHA = 0;
    deckRatingFire = 0;
    screenIndex = -1;
    screenScale = QPointF(1,1);

    this->numCaptured = 0;
    this->extendedCapture = true;

    //Force draft
    QList<DeckCard> deckCardList = deckHandler->getDeckCardList();
    initDeckCounters(deckCardList);
    newFindScreenLoop(true);    //Not from the saved screen settings: a rescan is asked when the plates are off
}


void DraftHandler::refreshHeroes()
{
    for(int i=0; i<3; i++)
    {
        cardDetected[i] = false;
        draftCardMaps[i].clear();
        bestMatchesMaps[i].clear();
        ocrCodes[i] = "";
        ocrUnmatchedText[i] = "";
    }

    numCaptured = 0;
    heroesShown = false;
    if(draftHeroWindow != nullptr)  draftHeroWindow->hideScores();

    newCaptureDraftLoop();
}


//The mascot's Rescan: looks for the screen again, then reads the heroes or cards on it
void DraftHandler::rescan()
{
    if(drafting)
    {
        refreshDraft();
        return;
    }
    if(!heroDrafting)   return;

    emit pDebug("\nRescan heroes.");
    deleteDraftHeroWindow();
    for(int i=0; i<3; i++)
    {
        cardDetected[i] = false;
        draftCardMaps[i].clear();
        bestMatchesMaps[i].clear();
        ocrCodes[i] = "";
        ocrUnmatchedText[i] = "";
        screenRects[i] = cv::Rect(0,0,0,0);
    }
    numCaptured = 0;
    screenIndex = -1;
    screenScale = QPointF(1,1);
    newFindScreenLoop(true);    //Not from the saved screen settings, they may be the wrong ones
}


PickScores DraftHandler::getPickScores()
{
    return pickScores;
}


QString DraftHandler::getDraftStatus()
{
    return draftStatus;
}


bool DraftHandler::isEmptyDeck()
{
    return (numDraftedCards == 0);
}


void DraftHandler::showHAScores(QString ogCodes[], QString cardNames[])
{
    int rating[3] = {0,0,0};
    for(int i=0; i<3; i++)  rating[i] = getHAScore(ogCodes[i]);

    showNewRatings(cardNames[0], cardNames[1], cardNames[2],
                   rating[0], rating[1], rating[2],
                   HearthArena);
}


//The class's arena cards with both scores, the reference of the pick ratings (the hands and the mascot's advice)
void DraftHandler::updatePickRatingPool()
{
    QList<PickRating::Card> pool;
    for(auto it=hearthArenaTiers.constBegin(); it!=hearthArenaTiers.constEnd(); it++)
    {
        const QString &code = it.key();
        PickRating::Card card;
        card.haScore = it.value();
        card.fireWinrate = (fireWRMap == nullptr) ? 0 : fireWRMap[this->arenaHero].value(code);
        card.fireGames = (fireSamplesMap == nullptr) ? 0 : fireSamplesMap[this->arenaHero].value(code);
        pool << card;
    }
    PickRating::setPool(pool);
}


int DraftHandler::getHAScore(const QString &code)
{
    return hearthArenaTiers[getHACode(code)];
}


QString DraftHandler::getHACode(QString code)
{
    if(!hearthArenaTiers.contains(code))
    {
        if(code.startsWith("CORE_") && hearthArenaTiers.contains(code.mid(5)))
            code = code.mid(5);
        else if(hearthArenaTiers.contains("CORE_" + code))
            code = "CORE_" + code;
        else
        {
            QString name = Utility::cardEnNameFromCode(code);
            const QStringList altCodes = Utility::cardEnCodesFromName(name);
            for(const QString &altCode: altCodes)
            {
                if(hearthArenaTiers.contains(altCode))
                {
                    code = altCode;
                }
            }
        }
    }
    return code;
}


QString DraftHandler::getFireCode(QString code)
{
    return getFireCode(code, this->arenaHero);
}
//The code Firestone has stats for: CORE_ and other reprints of the card share its name
QString DraftHandler::getFireCode(QString code, CardClass heroClass)
{
    auto &samplesMap = fireSamplesMap;
    //Stats not downloaded yet, e.g. a redraft continued right at startup
    if(samplesMap == nullptr)   return code;
    if(!samplesMap[heroClass].contains(code) || (samplesMap[heroClass][code] == 0))
    {
        if(code.startsWith("CORE_"))
        {
            QString noCoreCode = code.mid(5);
            if(samplesMap[heroClass].contains(noCoreCode) && (samplesMap[heroClass][noCoreCode] != 0))
            {
                return noCoreCode;
            }
        }
        {
            QString coreCode = "CORE_" + code;
            if(samplesMap[heroClass].contains(coreCode) && (samplesMap[heroClass][coreCode] != 0))
            {
                return coreCode;
            }
        }

        QString name = Utility::cardEnNameFromCode(code);
        const QStringList altCodes = Utility::cardEnCodesFromName(name);
        int maxIncluded = 0;
        for(const QString &altCode: altCodes)
        {
            if(samplesMap[heroClass].contains(altCode) && samplesMap[heroClass][altCode]>maxIncluded)
            {
                emit pDebug(QStringLiteral("%1 - %2 - not found on %3 data. Swap to %4 - %5")
                                .arg(code, name, "Fire", altCode, name));

                maxIncluded = samplesMap[heroClass][altCode];
                code = altCode;
            }
        }
    }
    return code;
}


void DraftHandler::showFireScores(QString ogCodes[], QString cardNames[])
{
    float wrFire[3] = {0,0,0};
    int samplesFire[3] = {0,0,0};

    if(fireWRMap != nullptr && fireSamplesMap != nullptr)
    {
        for(int i=0; i<3; i++)
        {
            QString code = getFireCode(ogCodes[i]);
            wrFire[i] = fireWRMap[this->arenaHero][code];
            samplesFire[i] = fireSamplesMap[this->arenaHero][code];

            //Legendary groups: no bundle averaging. The legendary is only drafted together with its group,
            //so the winrate of decks with the legendary is already the group winrate, while the group cards
            //are mostly drafted on their own and their winrates come from other decks.
        }
    }

    showNewRatings(cardNames[0], cardNames[1], cardNames[2],
                   wrFire[0], wrFire[1], wrFire[2],
                   FireStone,
                   samplesFire[0], samplesFire[1], samplesFire[2]);
}


void DraftHandler::showNewCards(DraftCard bestCards[])
{
    setDraftStatus("");
    for(int i=0; i<3; i++)  prevCodes[i] = "";

    //Load cards
    for(int i=0; i<3; i++)  draftCards[i] = bestCards[i];

    QString ogCodes[3];
    QString cardNames[3];
    for(int i=0; i<3; i++)
    {
        ogCodes[i] = bestCards[i].getCode();
        cardNames[i] = Utility::cardLocalNameFromCode(ogCodes[i]);
    }

    //The legendary groups screen: three legendaries (after picking a group the deck can still look empty)
    bool legendaryGroups = true;
    for(int i=0; i<3; i++)  if(Utility::getRarityFromCode(ogCodes[i]) != LEGENDARY)     legendaryGroups = false;
    pickScores = PickScores();
    if(draftScoreWindow != nullptr)     draftScoreWindow->setLegendaryGroups(legendaryGroups);
    if(draftScoreWindow != nullptr && !legendaryGroups && screenIndex >= 0 && screenIndex < QGuiApplication::screens().count())
    {
        //The found screen can be a bit off in scale: the names read on the cards place the plates
        const QRect screenGeometry = QGuiApplication::screens()[screenIndex]->geometry();
        QList<QPointF> nameCenters;
        for(int i=0; i<3; i++)
        {
            if(!ocrNameCodes[i].isEmpty() && ocrNameCodes[i] == ocrCodes[i] && ocrCodes[i] == degoldCode(ogCodes[i]))
            {
                nameCenters << QPointF(screenGeometry.x() + ocrNameCenters[i].x() * screenScale.x(),
                                       screenGeometry.y() + ocrNameCenters[i].y() * screenScale.y());
            }
            else    nameCenters << QPointF(-1, -1);
        }
        const double measured = draftScoreWindow->setNameCenters(nameCenters, plateScale);
        if(measured > 0)    plateScale = measured;
    }
    updatePickRatingPool();     //Before the scores: the plates' hands use the ratings
    showHAScores(ogCodes, cardNames);
    showFireScores(ogCodes, cardNames);
    pickScores.showFire = draftMethodFire;
    pickScores.showHA = draftMethodHA;
    pickScores.legendaryGroup = legendaryGroups;
    emit cardsScored();
}


void DraftHandler::updateDeckScore(float cardRatingHA, float cardRatingFire)
{
    deckRatingHA += static_cast<int>(cardRatingHA);
    deckRatingFire += cardRatingFire;
}


void DraftHandler::showNewRatings(const QString &cardName1, const QString &cardName2, const QString &cardName3,
                                    float rating1, float rating2, float rating3,
                                    DraftMethod draftMethod,
                                    int includedDecks1, int includedDecks2, int includedDecks3)
{
    QString cardNames[3] = {cardName1, cardName2, cardName3};
    float ratings[3] = {rating1,rating2,rating3};
    int includedDecks[3] = {includedDecks1, includedDecks2, includedDecks3};

    for(int i=0; i<3; i++)
    {
        pickScores.names[i] = cardNames[i];
        pickScores.codes[i] = draftCards[i].getCode();
        if(draftMethod == FireStone)
        {
            pickScores.fire[i] = ratings[i];
            pickScores.fireGames[i] = includedDecks[i];
        }
        else if(draftMethod == HearthArena)     pickScores.ha[i] = ratings[i];
    }

    //Mostrar score
    if(draftScoreWindow != nullptr)
    {
        draftScoreWindow->setScores(rating1, rating2, rating3, draftMethod, includedDecks1, includedDecks2, includedDecks3, isEmptyDeck());
    }
}


bool DraftHandler::areScreenRectsValid(cv::Mat &screenCapture, int length)
{
    QRect fullRect2(0, 0, screenCapture.cols, screenCapture.rows);
    for(int i=0; i<length; i++)
    {
        QRect rect(screenRects[i].x, screenRects[i].y, screenRects[i].width, screenRects[i].height);
        if(!fullRect2.contains(rect))
        {
            emit pDebug("ScreenRects out of bounds (screenCapture):", Warning);
            emit pDebug("[screenCapture](" +
                    QString::number(fullRect2.x()) + "," + QString::number(fullRect2.y()) + "," +
                    QString::number(fullRect2.width()) + "," + QString::number(fullRect2.height()) + ")");
            emit pDebug("[" + QString::number(i) + "](" +
                    QString::number(rect.x()) + "," + QString::number(rect.y()) + "," +
                    QString::number(rect.width()) + "," + QString::number(rect.height()) + ")");
            return false;
        }
    }
    return true;
}


bool DraftHandler::getScreenCardsHist(cv::MatND screenCardsHist[], int length)
{
    cv::Mat screenCapture = getScreenMat();
    if(screenCapture.empty() || !areScreenRectsValid(screenCapture, length))    return false;

    cv::Mat bigCards[5];
    for(int i=0; i<length; i++)     bigCards[i] = screenCapture(screenRects[i]);

// #ifdef QT_DEBUG
//     for(int i=0; i<length; i++)     cv::imshow("Card" + QString::number(i).toStdString(), bigCards[i]);
// #endif

    for(int i=0; i<length; i++)     screenCardsHist[i] = getHist(bigCards[i]);
    if(drafting && length == 3)             readCardNames(screenCapture);
    else if(heroDrafting && length == 3)    readHeroClasses(screenCapture);
    return true;
}


bool DraftHandler::isGoldCode(QString fileName)
{
    return fileName.endsWith("_premium");
}


QString DraftHandler::degoldCode(QString fileName)
{
    QString code = fileName;
    if(code.endsWith("_premium"))   code.chop(8);
    return code;
}


void DraftHandler::mapBestMatchingCodes(cv::MatND screenCardsHist[3])
{
    bool newCardsFound = false;
    bool codesSameAsPrev = ((QDateTime::currentSecsSinceEpoch() - prevCodesTime)<PREV_CODES_TIME);

#ifdef QT_DEBUG
    #if DEBUG_ALLOW_SAME_TRIO
        codesSameAsPrev = false;
    #endif
#endif

    const int numCandidates = (extendedCapture?CAPTURE_EXTENDED_CANDIDATES:CAPTURE_MIN_CANDIDATES);

    for(int i=0; i<3; i++)
    {
        QMultiMap<double, QString> bestMatchesMap;
        for(QMap<QString, cv::MatND>::const_iterator it=cardsHist.constBegin(); it!=cardsHist.constEnd(); it++)
        {
            QString code = it.key();

            if(drafting && multiclassArena && arenaHeroMulticlassPower != INVALID_CLASS)
            {
                QList<CardClass> cardClass = Utility::getClassFromCode(degoldCode(code));
                if(!(cardClass.contains(NEUTRAL) || cardClass.contains(arenaHero) ||
                     cardClass.contains(arenaHeroMulticlassPower))) continue;
            }

            double match = compareHist(screenCardsHist[i], it.value(), 3);
            bestMatchesMap.insert(match, code);

            //Actualizamos DraftCardMaps con los nuevos resultados
            if((numCaptured != 0) && draftCardMaps[i].contains(code))
            {
                draftCardMaps[i][code].setBestQualityMatch(match, false);
            }
        }

        //Incluimos en DraftCardMaps los mejores 7 matches, si no han sido ya actualizados por estar en el map.
        QList<double> bestMatchesList = bestMatchesMap.keys();
        for(int j=0; j<numCandidates && j<bestMatchesList.count(); j++)
        {
            double match = bestMatchesList.at(j);
            QString code = bestMatchesMap.value(match);

            if(!draftCardMaps[i].contains(code))
            {
                newCardsFound = true;
                draftCardMaps[i].insert(code, DraftCard(degoldCode(code), isGoldCode(code)));
                if(numCaptured != 0)    draftCardMaps[i][code].setBestQualityMatch(match, true);
            }

            if(codesSameAsPrev && j==0)
            {
                QString codeS = degoldCode(code);
                if(codeS != prevCodes[i])   codesSameAsPrev = false;
            }
        }
    }

    //No empezamos a contar si siguen apareciendo las mismas 3 cartas despues del ultimo pick
    if(codesSameAsPrev)
    {
        for(int i=0; i<3; i++)  draftCardMaps[i].clear();
        numCaptured = 0;
    }
    //No empezamos a contar mientras sigan apareciendo nuevas cartas en las 7 mejores posiciones
    else if(numCaptured != 0 || !newCardsFound)
    {
        if(numCaptured == 0)
        {
            for(int i=0; i<3; i++)  draftCardMaps[i].clear();
        }

        this->numCaptured++;
    }


//#ifdef QT_DEBUG
//    for(int i=0; i<3; i++)
//    {
//        qDebug()<<endl;
//        for(QString code: draftCardMaps[i].keys())
//        {
//            DraftCard card = draftCardMaps[i][code];
//            qDebug()<<"["<<i<<"]"<<code<<card.getName()<<" -- "<<
//                      (static_cast<int>(card.getBestQualityMatches()*1000))/1000.0;
//        }
//    }
//    qDebug()<<"Captured: "<<numCaptured<<endl;
//#endif
}


cv::MatND DraftHandler::getHist(const QString &code)
{
    cv::Mat fullCard = cv::imread((Utility::hscardsPath() + "/" + code + ".png").toStdString(), cv::IMREAD_COLOR);
    cv::Mat srcBase;
    if(drafting || redraftingReview)
    {
        if(code.endsWith("_premium"))
        {
            if(fullCard.cols<(59+82) || fullCard.rows<(70+82))
            {
                emit pDebug("Card premium cv::Rect overflow.");
                cv::MatND emptyHist;
                return emptyHist;
            }
            srcBase = fullCard(cv::Rect(59,70,82,82));
// #ifdef QT_DEBUG
//             if(code.startsWith("TLC_465"))
//             {
//                 cv::imshow("SrcP", srcBase);
//             }
// #endif
        }
        else
        {
            if(fullCard.cols<(59+82) || fullCard.rows<(70+82))
            {
                emit pDebug("Card cv::Rect overflow.");
                cv::MatND emptyHist;
                return emptyHist;
            }
            srcBase = fullCard(cv::Rect(59,70,82,82));
// #ifdef QT_DEBUG
//             if(code.startsWith("TLC_465"))
//             {
//                 cv::imshow("Src", srcBase);
//             }
// #endif
        }
    }
    return getHist(srcBase);
}


cv::MatND DraftHandler::getHist(const cv::Mat &srcBase)
{
    cv::Mat hsvBase;

    /// Convert to HSV
    cvtColor( srcBase, hsvBase, cv::COLOR_BGR2HSV );

    /// Using 50 bins for hue and 60 for saturation
    int h_bins = 50; int s_bins = 60;
    int histSize[] = { h_bins, s_bins };

    // hue varies from 0 to 179, saturation from 0 to 255
    float h_ranges[] = { 0, 180 };
    float s_ranges[] = { 0, 256 };
    const float* ranges[] = { h_ranges, s_ranges };

    // Use the o-th and 1-st channels
    int channels[] = { 0, 1 };

    /// Calculate the histograms for the HSV images
    cv::MatND histBase;
    calcHist( &hsvBase, 1, channels, cv::Mat(), histBase, 2, histSize, ranges, true, false );
    normalize( histBase, histBase, 0, 1, cv::NORM_MINMAX, -1, cv::Mat() );

    return histBase;
}


bool DraftHandler::screenFound()
{
    if(screenIndex != -1)   return true;
    else                    return false;
}


bool DraftHandler::loadTemplateSettings()
{
    QSettings settings;
    if(heroDrafting)
    {
        screenIndex = settings.value("heroDraftingScreenIndex", -1).toInt();
        QList<QScreen *> screens = QGuiApplication::screens();
        if(screenIndex >= screens.count() || screenIndex < 0)
        {
            screenIndex = -1;
            return false;
        }

        QRect rect;
        rect = settings.value("heroDraftingScreenRect0", QRect()).value<QRect>();
        screenRects[0]=cv::Rect(rect.x(), rect.y(), rect.width(), rect.height());
        rect = settings.value("heroDraftingScreenRect1", QRect()).value<QRect>();
        screenRects[1]=cv::Rect(rect.x(), rect.y(), rect.width(), rect.height());
        rect = settings.value("heroDraftingScreenRect2", QRect()).value<QRect>();
        screenRects[2]=cv::Rect(rect.x(), rect.y(), rect.width(), rect.height());

        screenScale = settings.value("heroDraftingScreenScale", QPointF(1,1)).value<QPointF>();
    }
    else// if(drafting)
    {
        screenIndex = settings.value("draftingScreenIndex", -1).toInt();
        QList<QScreen *> screens = QGuiApplication::screens();
        if(screenIndex >= screens.count() || screenIndex < 0)
        {
            screenIndex = -1;
            return false;
        }

        QRect rect;
        rect = settings.value("draftingScreenRect0", QRect()).value<QRect>();
        screenRects[0]=cv::Rect(rect.x(), rect.y(), rect.width(), rect.height());
        rect = settings.value("draftingScreenRect1", QRect()).value<QRect>();
        screenRects[1]=cv::Rect(rect.x(), rect.y(), rect.width(), rect.height());
        rect = settings.value("draftingScreenRect2", QRect()).value<QRect>();
        screenRects[2]=cv::Rect(rect.x(), rect.y(), rect.width(), rect.height());

        screenScale = settings.value("draftingScreenScale", QPointF(1,1)).value<QPointF>();
    }
    return true;
}


bool DraftHandler::saveTemplateSettings()
{
    if(screenIndex == -1)   return false;

    QSettings settings;
    if(heroDrafting)
    {
        settings.setValue("heroDraftingScreenIndex", screenIndex);

        QRect rect0(screenRects[0].x, screenRects[0].y, screenRects[0].width, screenRects[0].height);
        settings.setValue("heroDraftingScreenRect0", rect0);
        QRect rect1(screenRects[1].x, screenRects[1].y, screenRects[1].width, screenRects[1].height);
        settings.setValue("heroDraftingScreenRect1", rect1);
        QRect rect2(screenRects[2].x, screenRects[2].y, screenRects[2].width, screenRects[2].height);
        settings.setValue("heroDraftingScreenRect2", rect2);

        settings.setValue("heroDraftingScreenScale", screenScale);

        emit pDebug("Save HERO_DRAFT Screen Settings.");
    }
    else// if(drafting)
    {
        settings.setValue("draftingScreenIndex", screenIndex);

        QRect rect0(screenRects[0].x, screenRects[0].y, screenRects[0].width, screenRects[0].height);
        settings.setValue("draftingScreenRect0", rect0);
        QRect rect1(screenRects[1].x, screenRects[1].y, screenRects[1].width, screenRects[1].height);
        settings.setValue("draftingScreenRect1", rect1);
        QRect rect2(screenRects[2].x, screenRects[2].y, screenRects[2].width, screenRects[2].height);
        settings.setValue("draftingScreenRect2", rect2);

        settings.setValue("draftingScreenScale", screenScale);

        emit pDebug("Save DRAFT Screen Settings.");
    }
    return true;
}


bool DraftHandler::isFindScreenOk(ScreenDetection &screenDetection)
{
    //Prueba de fallos
//        "[0]" 438 274 119 118
//        "[1]" 719 274 119 118
//        "[2]" 999 274 119 118
//        DRAFT -> 0.109259 BIEN / HERO DRAFT -> 0.146296 BIEN
//        "[0]" 421 344 159 158
//        "[1]" 702 344 159 158
//        "[2]" 982 344 159 158
//        DRAFT -> 0.146296 MAL
    float maxDistortion;
    if(heroDrafting)            maxDistortion = 0.2;    //The 2026 portraits are about 0.17 of the screen height
    else if(redraftingReview)   maxDistortion = 0.119;
    else                        maxDistortion = 0.119;// if(drafting)
    for(int i=0; i<(redraftingReview?5:3); i++)
    {
        if(((screenDetection.screenRects[i].width/static_cast<float>(screenDetection.screenHeight)) > maxDistortion) ||
                ((screenDetection.screenRects[i].height/static_cast<float>(screenDetection.screenHeight)) > maxDistortion))
        {
            emit pDebug("WARNING: Hearthstone arena screen detected: Bad shape: "
                    "W(" + QString::number(screenDetection.screenRects[i].width) + "/" +
                    QString::number(screenDetection.screenHeight) +
                    "=" + QString::number(screenDetection.screenRects[i].width/static_cast<float>(screenDetection.screenHeight)) +
                    ") H(" + QString::number(screenDetection.screenRects[i].height) + "/" +
                    QString::number(screenDetection.screenHeight) +
                    "=" + QString::number(screenDetection.screenRects[i].height/static_cast<float>(screenDetection.screenHeight)) +
                    "). Retrying...");
            return false;
        }
    }

    //All slots have the same size on screen. A skewed homography (e.g. a frame caught during the draft intro
    //animation) gives slots of different sizes, and cards 2/3 lower or higher than card 1.
    const int numRects = redraftingReview?5:3;
    const cv::Rect *rects = screenDetection.screenRects;
    int minW = rects[0].width, maxW = rects[0].width, minH = rects[0].height, maxH = rects[0].height;
    for(int i=1; i<numRects; i++)
    {
        minW = std::min(minW, rects[i].width);  maxW = std::max(maxW, rects[i].width);
        minH = std::min(minH, rects[i].height); maxH = std::max(maxH, rects[i].height);
    }
    bool badShape = (maxW > minW*1.1 || maxH > minH*1.1);
    if(!redraftingReview)
    {
        //Three slots in a row, evenly spaced
        int dx1 = rects[1].x - rects[0].x;
        int dx2 = rects[2].x - rects[1].x;
        if(std::abs(dx1 - dx2) > dx1*0.1)   badShape = true;
        for(int i=1; i<3; i++)
        {
            if(std::abs(rects[i].y - rects[0].y) > rects[0].height*0.1)    badShape = true;
        }
    }
    if(badShape)
    {
        emit pDebug("WARNING: Hearthstone arena screen detected: Uneven slots (" +
                    QString::number(rects[0].x) + "," + QString::number(rects[0].y) + "," + QString::number(rects[0].width) + ") (" +
                    QString::number(rects[1].x) + "," + QString::number(rects[1].y) + "," + QString::number(rects[1].width) + ") (" +
                    QString::number(rects[2].x) + "," + QString::number(rects[2].y) + "," + QString::number(rects[2].width) + "). Retrying...");
        return false;
    }
    return true;
}


//Two detections in a row give the same slots: the screen is not moving anymore (draft intro animation finished).
bool DraftHandler::isFindScreenStable(ScreenDetection &screenDetection)
{
    bool stable = (prevScreenDetection.screenIndex == screenDetection.screenIndex);
    for(int i=0; i<(redraftingReview?5:3) && stable; i++)
    {
        const cv::Rect &a = prevScreenDetection.screenRects[i];
        const cv::Rect &b = screenDetection.screenRects[i];
        int maxDiff = std::max(3, b.width/20);
        if(std::abs(a.x - b.x) > maxDiff || std::abs(a.y - b.y) > maxDiff || std::abs(a.width - b.width) > maxDiff)
        {
            stable = false;
        }
    }
    prevScreenDetection = screenDetection;
    return stable;
}


bool DraftHandler::isFindScreenAsSettings(ScreenDetection &screenDetection)
{
    int maxDiff = screenRects[0].width/20;

    //Nunca se usa en redraftingReview asi que comparar los 3 primeros es suficiente
    emit pDebug("Screen Settings: I(" + QString::number(screenIndex) + ")");
    for(int i=0; i<3; i++)
        emit pDebug("[" + QString::number(i) + "](" +
                QString::number(screenRects[i].x) + "," + QString::number(screenRects[i].y) + "," +
                QString::number(screenRects[i].width) + "," + QString::number(screenRects[i].height) + ")");
    emit pDebug("Screen Found: I(" + QString::number(screenDetection.screenIndex) + ")");
    for(int i=0; i<3; i++)
        emit pDebug("[" + QString::number(i) + "](" +
                QString::number(screenDetection.screenRects[i].x) + "," + QString::number(screenDetection.screenRects[i].y) + "," +
                QString::number(screenDetection.screenRects[i].width) + "," + QString::number(screenDetection.screenRects[i].height) + ")");

    if(screenIndex != screenDetection.screenIndex)  return false;
    for(int i=0; i<3; i++)
    {
        if(std::abs(screenRects[i].x - screenDetection.screenRects[i].x) > maxDiff)             return false;
        if(std::abs(screenRects[i].y - screenDetection.screenRects[i].y) > maxDiff)             return false;
        if(std::abs(screenRects[i].width - screenDetection.screenRects[i].width) > maxDiff)     return false;
        if(std::abs(screenRects[i].height - screenDetection.screenRects[i].height) > maxDiff)   return false;
    }
    return true;
}


void DraftHandler::newFindScreenLoop(bool skipScreenSettings)
{
    stopLoops = false;
    findScreenFails = 0;

    //skipScreenSettings = Force Draft / Continue Draft: No mostramos puntuaciones antes de encontrar el template
    //para asegurarnos que no estamos en la screen intermedia de arena, previo a llegar al draft desde main menu.
    if(!skipScreenSettings && loadTemplateSettings())
    {
        emit pDebug("Hearthstone arena screen loaded from settings.");
        createDraftWindows();
        if(drafting || heroDrafting)    newCaptureDraftLoop();
        else    return;
    }
    else
    {
        emit pDebug("Hearthstone arena screen NOT loaded from settings.");
    }

    if(!findingFrame && (drafting || heroDrafting || redraftingReview))
    {
        findingFrame = true;
        startFindScreenRects();
    }
}


void DraftHandler::startFindScreenRects()
{
    if(stopLoops)
    {
        findingFrame = false;
        return;
    }
    if(!futureFindScreenRects.isRunning())
    {
        findScreenClock.start();
        findScreenStartMs = -1;
        findScreenCaptureMs = -1;
        futureFindScreenRects.setFuture(QtConcurrent::run(&DraftHandler::findScreenRects, this));
    }
}


void DraftHandler::finishFindScreenRects()
{
    ScreenDetection screenDetection = futureFindScreenRects.result();

    //Timing of a slow search: waiting for a pool thread, taking the screenshots, the rest is SIFT matching
    qint64 totalMs = findScreenClock.elapsed();
    if(totalMs > 3000)
    {
        emit pDebug(QStringLiteral("Slow arena screen search: %1 ms (thread start %2 ms, screenshots %3 ms).")
                        .arg(totalMs).arg(findScreenStartMs.load()).arg(findScreenCaptureMs.load()));
    }
    //macOS Game Mode (on for fullscreen games) slows every other app down 20-40 times: a search takes 15-40 s
    bool gameModeHint = false;
    gameModeHint = (totalMs > 5000) && draftCards[0].getCode().isEmpty() && MacFullScreenOverlay::isHearthstoneFullScreen();
    if(gameModeHint)
    {
        setDraftStatus("macOS Game Mode slows the tracker down. Turn it off with the gamepad icon in the menu bar.");
    }

    if(stopLoops)
    {
        findingFrame = false;
        return;
    }

    if(screenDetection.screenIndex == -1)
    {
        emit pDebug("Hearthstone arena screen not found. Retrying...");
        //Once a second: about 10 s without seeing it. A Rescan shows the cards while this loop checks the screen.
        //A legendary group's preview covers the cards: not seeing them is expected, the bundle watch has the status.
        if(isPickShown() || gameModeHint || bundlePreviewOpen)  {}
        else if(++findScreenFails >= 10)
            setDraftStatus("Can't see the arena screen. Check the Screen Recording permission.");
        else
            setDraftStatus("Looking for the arena screen...");
        QTimer::singleShot(FINDSCREEN_LOOP_TIME, this, SLOT(startFindScreenRects()));
    }
    else if(!isFindScreenOk(screenDetection))
    {
        QTimer::singleShot(FINDSCREEN_LOOP_TIME, this, SLOT(startFindScreenRects()));
    }
    else if(!isFindScreenStable(screenDetection))
    {
        emit pDebug("Hearthstone arena screen detected, waiting for a stable screen...");
        if(!isPickShown() && !gameModeHint && !bundlePreviewOpen)  setDraftStatus(heroDrafting?"Scanning heroes...":"Scanning cards...");
        QTimer::singleShot(FINDSCREEN_STABLE_TIME, this, SLOT(startFindScreenRects()));
    }
    else
    {
        prevScreenDetection = ScreenDetection();
        bool isSame = isFindScreenAsSettings(screenDetection);
        bool needCreate = (screenIndex == -1);
        findingFrame = false;
        this->screenIndex = screenDetection.screenIndex;
        this->screenScale = screenDetection.screenScale;

        for(int i=0; i<5; i++)
        {
            this->screenRects[i] = screenDetection.screenRects[i];
            this->manaRects[i] = screenDetection.manaRects[i];
            this->rarityRects[i] = screenDetection.rarityRects[i];
        }
        findScreenFails = 0;
        if(!isPickShown() && !bundlePreviewOpen)  setDraftStatus(heroDrafting?"Scanning heroes...":"Scanning cards...");
        emit pDebug("Hearthstone arena screen detected on screen " + QString::number(screenIndex) +
                    ". " + (isSame?QString("It's"):QString("Not")) + " the same.");

        if(needCreate)
        {
            if(!redraftingReview)   createDraftWindows();
            if(drafting || heroDrafting || redraftingReview)    newCaptureDraftLoop();

            //The slow end of the arena intro zoom passes the stable check: look once more, a moved screen
            //is "Not the same" and places the plates again
            if(drafting || heroDrafting)    checkScreenAgain("after the intro");
        }
        else
        {
            if(isSame)
            {
                showOverlay();
                startReviewBestCards();
            }
            else
            {
                createDraftWindows();

                if(drafting)            refreshCapturedCards();
                else if(heroDrafting)   refreshHeroes();
            }
        }
    }
}


ScreenDetection DraftHandler::findScreenRects()
{
    findScreenStartMs = findScreenClock.elapsed();
    //Pool threads run at the default QoS, which macOS moves to the efficiency cores while the app is in the background
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);

    //The class labels under the heroes place them better than the frame template, which can match a bit off
    if(heroDrafting)
    {
        ScreenDetection screenDetection;
        if(findHeroRectsByOcr(screenDetection))     return screenDetection;
    }

    std::vector<Point2f> templatePoints;
    if(heroDrafting)
    {
        templatePoints.resize(6);
        //The current hero choice: bigger portraits, further apart, than when heroesTemplate2.png was taken
        //(its frame still matches). Measured on the 2026 client.
        templatePoints[0] = cv::Point(152,227); templatePoints[1] = cv::Point(152+190,227+190);
        templatePoints[2] = cv::Point(457,227); templatePoints[3] = cv::Point(457+190,227+190);
        templatePoints[4] = cv::Point(759,227); templatePoints[5] = cv::Point(759+190,227+190);
    }
    else if(redraftingReview)
    {
        //1     4
        //  3
        //2     5
        templatePoints.resize(10);
        templatePoints[0] = cv::Point(142,241); templatePoints[1] = cv::Point(142+117,241+117);
        templatePoints[2] = cv::Point(146,671); templatePoints[3] = cv::Point(146+117,671+117);
        templatePoints[4] = cv::Point(480,378); templatePoints[5] = cv::Point(480+117,378+117);
        templatePoints[6] = cv::Point(819,241); templatePoints[7] = cv::Point(819+117,241+117);
        templatePoints[8] = cv::Point(819,671); templatePoints[9] = cv::Point(819+117,671+117);

        //Por ahora no hacemos comprobacion mana/rarity
        // templatePoints[10] = cv::Point(80,204); templatePoints[11] = cv::Point(80+34,204+45);
        // templatePoints[12] = cv::Point(83,637); templatePoints[13] = cv::Point(83+34,637+45);
        // templatePoints[14] = cv::Point(420,341); templatePoints[15] = cv::Point(420+34,341+45);
        // templatePoints[16] = cv::Point(762,204); templatePoints[17] = cv::Point(762+34,204+45);
        // templatePoints[18] = cv::Point(762,637); templatePoints[19] = cv::Point(762+34,637+45);

        // templatePoints[20] = cv::Point(197,403); templatePoints[21] = cv::Point(197+11,403+17);
        // templatePoints[22] = cv::Point(201,834); templatePoints[23] = cv::Point(201+11,834+17);
        // templatePoints[24] = cv::Point(537,540); templatePoints[25] = cv::Point(537+11,540+17);
        // templatePoints[26] = cv::Point(877,403); templatePoints[27] = cv::Point(877+11,403+17);
        // templatePoints[28] = cv::Point(877,834); templatePoints[29] = cv::Point(877+11,834+17);
    }
    else// if(drafting)
    {
        templatePoints.resize(18);
        templatePoints[0] = cv::Point(234,263); templatePoints[1] = cv::Point(234+114,263+114);
        templatePoints[2] = cv::Point(512,263); templatePoints[3] = cv::Point(512+114,263+114);
        templatePoints[4] = cv::Point(789,263); templatePoints[5] = cv::Point(789+114,263+114);

        templatePoints[6] = cv::Point(175,227); templatePoints[7] = cv::Point(175+33,227+44);
        templatePoints[8] = cv::Point(454,227); templatePoints[9] = cv::Point(454+33,227+44);
        templatePoints[10] = cv::Point(733,227); templatePoints[11] = cv::Point(733+33,227+44);

        templatePoints[12] = cv::Point(288,420); templatePoints[13] = cv::Point(288+11,420+17);
        templatePoints[14] = cv::Point(566,420); templatePoints[15] = cv::Point(566+11,420+17);
        templatePoints[16] = cv::Point(844,420); templatePoints[17] = cv::Point(844+11,420+17);
    }


    QList<QFuture<SDBasic>> futureList;
    QList<QScreen *> screens = QGuiApplication::screens();

    QScreen *screen = nullptr;
    QImage image;
    int screenIndex=0;

    //One job per screen: the screenshot's SIFT features are computed once and matched against every template
    auto findTemplates = [&](const QStringList &arenaTemplates) {
        futureList.append(QtConcurrent::run([=]() { // <-- [=] captura por valor
            pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
            ScreenFeatures features = Utility::screenFeatures(screen, image);
            SDBasic bestSdb;
            bestSdb.goodMatches = -1;
            for(const QString &arenaTemplate: arenaTemplates)
            {
                SDBasic sdb;
                sdb.screenPoints = Utility::findTemplateOnScreen(arenaTemplate, features, templatePoints, sdb.goodMatches);
                sdb.screenScale = features.screenScale;
                sdb.screenHeight = features.screenHeight;
                sdb.screenIndex = screenIndex;
                if(sdb.goodMatches > bestSdb.goodMatches)   bestSdb = sdb;
            }
            return bestSdb;
        }));
    };
    auto findDraftTemplates = [&]() {
        if(redraftingReview)    findTemplates({"redraftTemplate.png"});
        //Only heroesTemplate2.png matches the current frame; the hero points are measured on it
        else if(heroDrafting)   findTemplates({"heroesTemplate2.png"});
        else /*if(drafting)*/   findTemplates({"arenaTemplate.png", "arenaTemplate2.png"});
    };

    bool inWayland = false;

    if(inWayland)
    {
        screen = screens[screenIndex];
        image = Utility::getScreenshot(screen);
        findDraftTemplates();
    }
    else
    {
        for(screenIndex=0; screenIndex<screens.count(); screenIndex++)
        {
            screen = screens[screenIndex];
            if (!screen)    continue;
            QElapsedTimer captureClock;
            captureClock.start();
            image = Utility::getScreenshot(screen);
            findScreenCaptureMs = std::max<qint64>(findScreenCaptureMs, 0) + captureClock.elapsed();
            if(image.isNull())  continue;
            findDraftTemplates();
        }
    }

    //Cogemos el mejor y verificamos si es valido
    SDBasic bestSdb;
    int bestGoodMatches = -1;
    for(QFuture<SDBasic> &future: futureList)
    {
        future.waitForFinished();
        SDBasic sdb = future.result();
        // qDebug()<<"GOOD MATCHES:"<<sdb.goodMatches<<sdb.screenIndex;
        if(sdb.goodMatches > bestGoodMatches)
        {
            bestGoodMatches = sdb.goodMatches;
            bestSdb = sdb;
        }
    }

    ScreenDetection screenDetection;
    std::vector<Point2f> &screenPoints  = bestSdb.screenPoints;
    screenDetection.screenScale = bestSdb.screenScale;
    screenDetection.screenHeight = bestSdb.screenHeight;
    screenDetection.screenIndex = bestSdb.screenIndex;


    bool templateFound = (!screenPoints.empty() && areScreenPointsValid(screenPoints, screenDetection.screenHeight));
    if(templateFound)
    {
        //Calculamos screenRects, manaRects y rarityRects
        if(heroDrafting)
        {
            for(int i=0; i<3; i++)
            {
                screenDetection.screenRects[i]=cv::Rect(screenPoints[static_cast<ulong>(i*2)], screenPoints[static_cast<ulong>(i*2+1)]);
            }
        }
        else if(redraftingReview)
        {
            for(int i=0; i<5; i++)
            {
                screenDetection.screenRects[i]=cv::Rect(screenPoints[static_cast<ulong>(i*2)], screenPoints[static_cast<ulong>(i*2+1)]);
                //Por ahora no hacemos comprobacion mana/rarity
                // screenDetection.manaRects[i]=cv::Rect(screenPoints[static_cast<ulong>((i+5)*2)], screenPoints[static_cast<ulong>((i+5)*2+1)]);
                // screenDetection.rarityRects[i]=cv::Rect(screenPoints[static_cast<ulong>((i+10)*2)], screenPoints[static_cast<ulong>((i+10)*2+1)]);
            }
        }
        else// if(drafting)
        {
            for(int i=0; i<3; i++)
            {
                screenDetection.screenRects[i]=cv::Rect(screenPoints[static_cast<ulong>(i*2)], screenPoints[static_cast<ulong>(i*2+1)]);
                screenDetection.manaRects[i]=cv::Rect(screenPoints[static_cast<ulong>((i+3)*2)], screenPoints[static_cast<ulong>((i+3)*2+1)]);
                screenDetection.rarityRects[i]=cv::Rect(screenPoints[static_cast<ulong>((i+6)*2)], screenPoints[static_cast<ulong>((i+6)*2+1)]);
            }
        }


        // DEBUG imshow
        // for(int i=0; i<5; i++)
        // {
        //     QRect rect = screen->geometry();
        //     QImage image = QGuiApplication::primaryScreen()->grabWindow(0,rect.x(),rect.y(),rect.width(),rect.height()).toImage();
        //     cv::Mat mat(image.height(),image.width(),CV_8UC4,image.bits(), static_cast<size_t>(image.bytesPerLine()));
        //     cv::Rect rectSmall = screenDetection.screenRects[i];
        //     cv::Mat orig = mat(cv::Rect(rectSmall.x, rectSmall.y, rectSmall.width, rectSmall.height));
        //     imshow(QString::number(i).toStdString() + "orig", orig);
        // }

        return screenDetection;
    }
    else
    {
        screenDetection.screenIndex = -1;
        return screenDetection;
    }
}


//The hero slots from the class labels on the banners under the portraits (enUS), read in the Hearthstone window.
//Each portrait square is above its label, sized by the distance between labels (measured on the 2026 client:
//side 0.625 and center 0.505 above the label, in label distances).
bool DraftHandler::findHeroRectsByOcr(ScreenDetection &screenDetection)
{
    if(Utility::getLocalLang() != "enUS")   return false;
    const QRect hsRect = MacOcr::hearthstoneWindowRect();
    QScreen *screen = hsRect.isNull() ? nullptr : QGuiApplication::screenAt(hsRect.center());
    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    if(screen == nullptr || primaryScreen == nullptr)   return false;

    QImage image = primaryScreen->grabWindow(0, hsRect.x(), hsRect.y(), hsRect.width(), hsRect.height()).toImage();
    if(image.isNull())  return false;
    hideTrackerWindows(image, hsRect);
    if(image.width() > 1400)    image = image.scaledToWidth(1400, Qt::SmoothTransformation);
    const QList<MacOcr::TextLine> lines = MacOcr::recognizeTextLines(image, "enUS");

    static const QStringList classNames = {"DEATHKNIGHT", "DEMONHUNTER", "DRUID", "HUNTER", "MAGE", "PALADIN",
                                           "PRIEST", "ROGUE", "SHAMAN", "WARLOCK", "WARRIOR"};
    auto letters = [](const QString &text) {
        QString result;
        for(const QChar &c: text.toUpper())     if(c.isLetter())    result += c;
        return result;
    };

    //A label can be split in two lines (DEMON / HUNTER)
    QList<QPair<QString, QRectF>> labels;
    QList<int> used;
    for(int i=0; i<lines.count(); i++)
    {
        if(used.contains(i))    continue;
        const QString text = letters(lines[i].text);
        QRectF rect = lines[i].rect;
        QString heroClass = classNames.contains(text) ? text : QString();
        if(text == "DEMON" || text == "DEATH")
        {
            for(int j=0; j<lines.count(); j++)
            {
                const QRectF &below = lines[j].rect;
                const QString merged = text + letters(lines[j].text);
                if(j != i && classNames.contains(merged) && below.top() > rect.top() &&
                        below.top() - rect.bottom() < rect.height() && qAbs(below.center().x() - rect.center().x()) < rect.width())
                {
                    heroClass = merged;
                    rect = rect.united(below);
                    used << j;
                    break;
                }
            }
        }
        if(!heroClass.isEmpty())    labels << qMakePair(heroClass, rect);
    }
    if(labels.count() != 3)     return false;
    std::sort(labels.begin(), labels.end(), [](const QPair<QString, QRectF> &a, const QPair<QString, QRectF> &b) {
        return a.second.center().x() < b.second.center().x();
    });

    //Three labels in a row, evenly spaced
    const qreal gap1 = labels[1].second.center().x() - labels[0].second.center().x();
    const qreal gap2 = labels[2].second.center().x() - labels[1].second.center().x();
    const qreal gap = (gap1 + gap2)/2;
    if(gap <= 0 || qAbs(gap1 - gap2) > gap*0.1)     return false;
    for(int i=1; i<3; i++)  if(qAbs(labels[i].second.center().y() - labels[0].second.center().y()) > gap*0.1)  return false;

    //Hearthstone window image pixels --> screenshot pixels of its screen
    const QRect screenRect = screen->geometry();
    const QImage screenshot = Utility::getScreenshot(screen);
    if(screenshot.isNull())     return false;
    const qreal pxPerPoint = screenshot.width() / static_cast<qreal>(screenRect.width());
    const qreal pointsPerImagePx = hsRect.width() / static_cast<qreal>(image.width());
    auto toScreenPx = [&](const QPointF &p) {
        return QPointF((hsRect.x() - screenRect.x() + p.x()*pointsPerImagePx) * pxPerPoint,
                       (hsRect.y() - screenRect.y() + p.y()*pointsPerImagePx) * pxPerPoint);
    };
    const qreal side = gap*0.625 * pointsPerImagePx * pxPerPoint;
    for(int i=0; i<3; i++)
    {
        const QPointF labelCenter = labels[i].second.center();
        const QPointF center = toScreenPx(QPointF(labelCenter.x(), labelCenter.y() - gap*0.505));
        screenDetection.screenRects[i] = cv::Rect(qRound(center.x() - side/2), qRound(center.y() - side/2), qRound(side), qRound(side));
    }
    screenDetection.screenIndex = QGuiApplication::screens().indexOf(screen);
    screenDetection.screenHeight = screenshot.height();
    screenDetection.screenScale = QPointF(screenRect.width() / static_cast<qreal>(screenshot.width()),
                                          screenRect.height() / static_cast<qreal>(screenshot.height()));
    emit pDebug("Hero slots found by OCR: " + labels[0].first + " " + labels[1].first + " " + labels[2].first);
    return true;
}


bool DraftHandler::areScreenPointsValid(std::vector<Point2f> screenPoints, int screenHeight)
{
    for(int i=0; i<3; i++)
    {
        int x1 = screenPoints[static_cast<ulong>(i*2)].x;
        int y1 = screenPoints[static_cast<ulong>(i*2)].y;
        int x2 = screenPoints[static_cast<ulong>(i*2+1)].x;
        int y2 = screenPoints[static_cast<ulong>(i*2+1)].y;

        // qDebug()<<"SCREENPOINTS"<<i<<":"<<x1<<y1<<"-"<<x2<<y2;

        if(x1>x2 || y1>y2 ||
            (((x2-x1)*15)<screenHeight) ||
            (((y2-y1)*15)<screenHeight)
            )
        {
            return false;
        }
    }
    return true;
}


void DraftHandler::beginHeroDraft()
{
    emit pDebug("Begin hero draft.");
    findScreenFails = 0;

    clearLists(false);
    this->heroDrafting = true;
    this->heroesShown = false;


    QList<DeckCard> deckCardList;
    initCodesAndHistMaps(deckCardList, true);
}


void DraftHandler::endHeroDraft()
{
    if(!heroDrafting)    return;

    emit pDebug("End hero draft.");

    clearLists(false);

    this->heroDrafting = false;
    deleteDraftHeroWindow();
}


//Class winrates arrived after the hero choice was shown
void DraftHandler::updateHeroScores()
{
    if(!heroDrafting || draftHeroWindow == nullptr) return;

    int classOrder[3];
    for(int i=0; i<3; i++)
    {
        if(bestMatchesMaps[i].isEmpty())    return;
        QString HSRkey = Utility::getCardAttribute(bestMatchesMaps[i].first(), "cardClass").toString();
        classOrder[i] = Utility::className2classOrder(HSRkey);
    }
    draftHeroWindow->setScores(classOrder);
    heroesShown = true;
    emit heroesScored(classOrder[0], classOrder[1], classOrder[2]);
}


void DraftHandler::showNewHeroes()
{
    int classOrder[3];
    for(int i=0; i<3; i++)
    {
        double match = bestMatchesMaps[i].firstKey();
        QString code = bestMatchesMaps[i].first();
        QString name = draftCardMaps[i][code].getName();
        QString cardInfo = code + " " + name + " " +
                QString::number(static_cast<int>(match*1000)/1000.0);
        emit pDebug("Choose: " + cardInfo);

        QString HSRkey = Utility::getCardAttribute(code, "cardClass").toString();
        classOrder[i] = Utility::className2classOrder(HSRkey);
    }
    if(draftHeroWindow != nullptr)     draftHeroWindow->setScores(classOrder);
    heroesShown = true;
    emit heroesScored(classOrder[0], classOrder[1], classOrder[2]);
}


void DraftHandler::createDraftWindows()
{
    deleteDraftHeroWindow();
    deleteDraftScoreWindow();

    //Screen out of index
    if(screenIndex >= QGuiApplication::screens().count() || screenIndex < 0)
    {
        emit pDebug("ScreenIndex/ScreenRects out of bounds while creating drafting windows.", Warning);
        return;
    }

    QPoint topLeft(static_cast<int>(screenRects[0].x * screenScale.x()), static_cast<int>(screenRects[0].y * screenScale.y()));
    QPoint bottomRight(static_cast<int>(screenRects[2].x * screenScale.x() + screenRects[2].width * screenScale.x()),
            static_cast<int>(screenRects[2].y * screenScale.y() + screenRects[2].height * screenScale.y()));
    QRect draftRect(topLeft, bottomRight);
    QSize sizeCard(static_cast<int>(screenRects[0].width * screenScale.x()), static_cast<int>(screenRects[0].height * screenScale.y()));

    QMainWindow *mainWindow = static_cast<QMainWindow *>(this->parent());

    if(drafting)
    {
        emit pDebug("Create drafting windows.");
        draftScoreWindow = new DraftScoreWindow(mainWindow, draftRect, sizeCard, screenIndex);
        connect(draftScoreWindow, SIGNAL(pDebug(QString,DebugLevel,QString)),
                this, SIGNAL(pDebug(QString,DebugLevel,QString)));
        draftScoreWindow->setDraftMethod(this->draftMethodHA, this->draftMethodFire);
    }
    else if(heroDrafting)
    {
        emit pDebug("Create heroDrafting windows.");
        QRect screenGeometry = QGuiApplication::screens()[screenIndex]->geometry();
        QList<QRect> heroRects;
        for(int i=0; i<3; i++)
        {
            heroRects << QRect(static_cast<int>(screenGeometry.x() + screenRects[i].x * screenScale.x()),
                               static_cast<int>(screenGeometry.y() + screenRects[i].y * screenScale.y()),
                               static_cast<int>(screenRects[i].width * screenScale.x()),
                               static_cast<int>(screenRects[i].height * screenScale.y()));
        }
        draftHeroWindow = new DraftHeroWindow(mainWindow, heroRects);

        connect(draftHeroWindow, SIGNAL(pDebug(QString,DebugLevel,QString)),
                this, SIGNAL(pDebug(QString,DebugLevel,QString)));
    }

    showOverlay();
}


void DraftHandler::setShowDraftScoresOverlay(bool value)
{
    this->showDraftScoresOverlay = value;
    showOverlay();
}


void DraftHandler::showOverlay()
{
    if(this->draftHeroWindow != nullptr)
    {
        this->draftHeroWindow->show();
    }
    if(this->draftScoreWindow != nullptr)
    {
        if(showDraftScoresOverlay)  this->draftScoreWindow->show();
        else                        this->draftScoreWindow->hide();
    }
}


void DraftHandler::setShowMyWR(bool value)
{
    this->showMyWR = value;
}


void DraftHandler::setDraftMethod(bool draftMethodHA, bool draftMethodFire)
{
    this->draftMethodHA = draftMethodHA;
    this->draftMethodFire = draftMethodFire;

    if(redrafting)      updateRedraftRemoveList();
    if(!isDrafting())   return;

    if(draftScoreWindow != nullptr)
    {
        draftScoreWindow->setDraftMethod(draftMethodHA, draftMethodFire);
    }
}


bool DraftHandler::isDrafting()
{
    return this->drafting;
}


//The current heroes or cards are already scored: a screen check must not say they're being scanned again
bool DraftHandler::isPickShown()
{
    return heroDrafting ? heroesShown : !draftCards[0].getCode().isEmpty();
}


//A redraft is offered but its pick screen hasn't been seen yet. Only macOS reads card names; elsewhere the histograms decide.
bool DraftHandler::isRedraftOffered()
{
    return redrafting && drafting && !redraftPicksSeen;
}


bool DraftHandler::isRedrafting()
{
    return this->redrafting;
}


void DraftHandler::setFireWRMap(QMap<QString, float> fireWRMap[])
{
    this->fireWRMap = fireWRMap;
}
void DraftHandler::setFireSamplesMap(QMap<QString, int> fireSamplesMap[])
{
    this->fireSamplesMap = fireSamplesMap;
}


void DraftHandler::buildHeroCodesList()
{
    heroCodesList.clear();

    //--------------------------------------------------------
    //----NEW HERO CLASS
    //--------------------------------------------------------
    for(const QString &code: (const QStringList)Utility::getSetCodes("HERO_SKINS", false, true))
    {
        heroCodesList.append(code);
    }
//    qDebug()<<endl<<"HERO CODES"<<heroCodesList.count()<<"!!!!!!!!!!!!!!!!!!!!!!!!"<<endl<<heroCodesList<<endl;
}


//Funciones para calcular deck scores fuera de draft (enemy decks)
void DraftHandler::initTierLists(const CardClass &heroClass)
{
    QStringList arenaCodes = Utility::getAllArenaCodes();
    initLightForgeTiers(heroClass, multiclassArena, arenaCodes, false);
    initHearthArenaTiers(heroClass, multiclassArena);
}


//Review Best Cards hebra
void DraftHandler::startReviewBestCards()
{
    if(futureReviewBestCards.isRunning())
    {
        emit pDebug("reviewBestCards: Avoid new run, already running.");
        return;
    }

    //The worker thread gets copies: the maps change in the GUI thread on each pick
    QList<QList<DraftCard>> candidates;
    QList<DraftCard> slotCards;
    for(int i=0; i<3; i++)
    {
        slotCards << draftCards[i];
        QList<DraftCard> slotCandidates;
        for(const QString &code: (const QList<QString>)bestMatchesMaps[i].values())
        {
            slotCandidates << draftCardMaps[i][code];
        }
        candidates << slotCandidates;
    }
    futureReviewBestCards.setFuture(QtConcurrent::run(&DraftHandler::reviewBestCards, this, candidates, slotCards));
}
void DraftHandler::finishReviewBestCards()
{
    QList<ReviewSlot> reviewSlots = futureReviewBestCards.result();
    if(reviewSlots.isEmpty() || capturing)
    {
        emit pDebug("reviewBestCards: manaRects/draftCards not ready or picked a card.");
        return;
    }

    QString bestCodes[3];
    for(int i=0; i<3; i++)
    {
        const ReviewSlot &reviewSlot = reviewSlots[i];
        //The slot changed while reviewing
        if(draftCards[i].getCode() != reviewSlot.slotCode)  continue;

        const QString newCode = reviewSlot.newCard.getCode();
        if(!newCode.isEmpty())
        {
            DraftCard newCard = reviewSlot.newCard;
            bestMatchesMaps[i].insert(1, newCode);
            draftCardMaps[i].insert(newCode, newCard);
        }
        bestCodes[i] = reviewSlot.code;
    }

    bool needShowCards = false;

    for(int i=0; i<3; i++)
    {
        //A card identified by its name is more reliable than the mana/rarity check: keep it
        if(!bestCodes[i].isEmpty() && !ocrCodes[i].isEmpty() && degoldCode(draftCards[i].getCode()) == ocrCodes[i])
        {
            emit pDebug("reviewBestCards: keep OCR card " + ocrCodes[i] + " in slot " + QString::number(i+1) +
                        " (mana/rarity review suggested " + bestCodes[i] + ")");
            bestCodes[i] = "";
        }
        if(!bestCodes[i].isEmpty())
        {
            needShowCards = true;
        }
    }

    if(needShowCards)
    {
        DraftCard bestCards[3];
        if(draftScoreWindow != nullptr)    draftScoreWindow->hideScores();
        for(int i=0; i<3; i++)
        {
            if(!bestCodes[i].isEmpty())
            {
                bestCards[i] = DraftCard(bestCodes[i]);
            }
            else    bestCards[i] = draftCards[i];
        }

        showNewCards(bestCards);
    }
}


//Worker thread: only reads its arguments and the screen, returns an empty list to abort
QList<ReviewSlot> DraftHandler::reviewBestCards(QList<QList<DraftCard>> candidates, QList<DraftCard> slotCards)
{
    //manaRect no iniciado, estamos leyendo screenRects de settings
    if(manaRects[1].width<1 || manaRects[1].height<1 || slotCards[0].getCode().isEmpty())
    {
        return {};
    }

    const cv::Mat screenBig = getScreenMat();
    if(screenBig.empty())   return {};

    double fx = 24.0/manaRects[1].width;
    double fy = 32.0/manaRects[1].height;
    cv::Mat screenSmall;
    resize(screenBig, screenSmall, Size(), fx, fy, cv::INTER_AREA);

    int legendaries = 0;
    for(DraftCard &slotCard: slotCards)     if(slotCard.getRarity() == LEGENDARY)  legendaries++;
    const bool posibleLegendaryPack = (legendaries > 1);

    QList<ReviewSlot> reviewSlots;
    for(int i=0; i<3; i++)
    {
        ReviewSlot reviewSlot;
        reviewSlot.slotCode = slotCards[i].getCode();

        int imgMana;
        CardRarity imgRarity;
        const cv::Rect manaRectSmall = cv::Rect(manaRects[i].x*fx, manaRects[i].y*fy, 24, 32);
        const cv::Rect rarityRectSmall = cv::Rect(rarityRects[i].x*fx, rarityRects[i].y*fy, 8, 12);
        getBestNManaRarity(imgMana, imgRarity, screenSmall, manaTemplates, rarityTemplates, manaRectSmall, rarityRectSmall);

        int cardMana = slotCards[i].getCost();
        CardRarity cardRarity = slotCards[i].getRarity();
        bool signatureImage = slotCards[i].isGold() && isSignatureCard(slotCards[i].getCode());
        bool validDraftCard = !(cardRarity == LEGENDARY && !posibleLegendaryPack);
        if(validDraftCard && (cardRarity == FREE || signatureImage))    imgRarity = INVALID_RARITY;

        if(imgMana != cardMana || imgRarity != cardRarity)
        {
            //Warning - Nueva carta
            if((cardMana < 10 || !validDraftCard) && (imgMana != -1))
            {
                QString slotCode = reviewSlot.slotCode;
                reviewSlot = getBestMatchManaRarity(candidates[i], i, screenBig, imgMana, imgRarity);
                reviewSlot.slotCode = slotCode;
            }
            //Warning - Misma carta
            else
            {
                reviewSlot.code = reviewSlot.slotCode;
            }
        }
        //No Warning - Misma carta (code = "")
        reviewSlots << reviewSlot;

        //Card picked while review
        if(capturing)   return {};
    }
    return reviewSlots;
}


ReviewSlot DraftHandler::getBestMatchManaRarity(QList<DraftCard> candidates, const int pos, const cv::Mat &screenBig,
                                                const int imgMana, const CardRarity imgRarity)
{
    ReviewSlot reviewSlot;
    for(int i=0; i<candidates.count(); i++)
    {
        if(candidates[i].getCost() == imgMana &&
                (imgRarity == INVALID_RARITY || candidates[i].getRarity() == imgRarity))
        {
            //Es la primera opcion, no mostramos warning
            if(i != 0)  reviewSlot.code = candidates[i].getCode();
            return reviewSlot;
        }
    }

    const cv::MatND screenCardHist = getHist(screenBig(screenRects[pos]));
    DraftCard draftCard = getBestAllMatchManaRarity(screenCardHist, imgMana, imgRarity);
    draftCard.setBestQualityMatch(1, true);
    reviewSlot.code = draftCard.getCode();
    if(!reviewSlot.code.isEmpty())  reviewSlot.newCard = draftCard;
    return reviewSlot;
}


DraftCard DraftHandler::getBestAllMatchManaRarity(const cv::MatND &screenCardHist, const int imgMana, const CardRarity imgRarity)
{
    double bestMatch = 1;
    QString bestCode = "";
    bool bestGold = false;

    for(QMap<QString, cv::MatND>::const_iterator it=cardsHist.constBegin(); it!=cardsHist.constEnd(); it++)
    {
        QString code = degoldCode(it.key());
        bool gold = isGoldCode(it.key());

        if(multiclassArena && arenaHeroMulticlassPower != INVALID_CLASS)
        {
            QList<CardClass> cardClass = Utility::getClassFromCode(code);
            if(!(cardClass.contains(NEUTRAL) || cardClass.contains(arenaHero) ||
                 cardClass.contains(arenaHeroMulticlassPower))) continue;
        }

        int cost = Utility::getCardAttribute(code, "cost").toInt();
        CardRarity rarity = Utility::getRarityFromCode(code);

        if(cost == imgMana &&
                (imgRarity == INVALID_RARITY || rarity == imgRarity))
        {
            double match = compareHist(screenCardHist, it.value(), 3);
            if(match < bestMatch)
            {
                bestMatch = match;
                bestCode = code;
                bestGold = gold;
            }
        }
    }
    return DraftCard(bestCode, bestGold);
}


void DraftHandler::getBestNManaRarity(int &manaN, CardRarity &cardRarity, const cv::Mat &screenSmall,
                                      const QList<cv::Mat> &manaTemplates, const QList<cv::Mat> &rarityTemplates,
                                      const cv::Rect &manaRectSmall, const cv::Rect &rarityRectSmall)
{
    int bestNMana, bestNRarity;
    double bestL2Mana, bestL2Rarity;
    getBestN(bestNMana, bestL2Mana, manaRectSmall, screenSmall, manaTemplates, manaTemplates.count());
    getBestN(bestNRarity, bestL2Rarity, rarityRectSmall, screenSmall, rarityTemplates, rarityTemplates.count());

    if(bestL2Mana<MANA_L2_THRESHOLD)        manaN = bestNMana;
    else                                    manaN = -1;
    if(bestL2Rarity<RARITY_L2_THRESHOLD)    cardRarity = static_cast<CardRarity>(bestNRarity);
    else                                    cardRarity = INVALID_RARITY;
}


void DraftHandler::getBestN(int &bestNs, double &bestL2s, const cv::Rect &rectSmall,
                                const cv::Mat &screenCapture, const QList<cv::Mat> &matTemplates, const int numTemplates)
{
    const float l2valid = 3.5;
    bool maxJumpReached = false;
    double centerBest, best = 10;
    const int initJump = 1;
    int bestX, bestY, bestN = -1, jump = 1;
    int centerX = 0, centerY = 0;
    int startX, startY, endX, endY;
    int prevX = centerX+2*jump+1, prevY = centerY+2*jump+1;
    setStartEndLoop(startX, startY, endX, endY, centerX, centerY, jump);
    getBestNOnRect(rectSmall, centerX, centerY, screenCapture, matTemplates, numTemplates, best, bestX, bestY, bestN);
    centerBest = best;

    while(jump>0 && abs(centerX)<initJump*16 && abs(centerY)<initJump*16)
    {
        for(int x=startX; x<=endX; x+=jump)
        {
            for(int y=startY; y<=endY; y+=jump)
            {
                if(x==centerX && y==centerY)    continue;
                if(abs(prevX-x)<=jump && abs(prevY-y)<=jump)    continue;
                getBestNOnRect(rectSmall, x, y, screenCapture, matTemplates, numTemplates, best, bestX, bestY, bestN);
            }
        }

        if(!maxJumpReached && best>l2valid && (jump == initJump*4))
        {
            jump/=2;
            prevX = centerX;
            prevY = centerY;
            best = centerBest;
            bestX = centerX;
            bestY = centerY;
            setStartEndLoop(startX, startY, endX, endY, centerX, centerY, jump*4);
            maxJumpReached = true;
        }
        else
        {
            if(!maxJumpReached && best>l2valid)
            {
                jump*=2;
                prevX = centerX+2*jump+1;
                prevY = centerY+2*jump+1;
                best = centerBest;
                bestX = centerX;
                bestY = centerY;
            }
            else if(centerBest == best)
            {
                jump/=2;
                prevX = centerX+2*jump+1;
                prevY = centerY+2*jump+1;
            }
            else
            {
                prevX = centerX;
                prevY = centerY;
                centerX = bestX;
                centerY = bestY;
                centerBest = best;

                //Despues de hacer AREA pasamos a jump 1
                if(maxJumpReached && (jump == initJump*2))  jump/=2;
            }
            setStartEndLoop(startX, startY, endX, endY, centerX, centerY, jump);
        }
    }
//    qDebug()<<"("<<bestX<<bestY<<") M:"<<bestN<<"L2:"<<best;
    bestNs = bestN;
    bestL2s = best;

    //Muestra la mejor coincidencia
//        cv::Mat orig = screenCapture(cv::Rect(rectSmall[i].x + bestX, rectSmall[i].y + bestY, rectSmall[i].width, rectSmall[i].height));
//        imshow(QString::number(i).toStdString() + "orig", orig);
}


void DraftHandler::setStartEndLoop(int &startX, int &startY, int &endX, int &endY,
                                   const int centerX, const int centerY, const int jump)
{
    startX = centerX-jump;
    endX = centerX+jump;
    startY = centerY-jump;
    endY = centerY+jump;
}


void DraftHandler::getBestNOnRect(const cv::Rect &rect, const int xOff, const int yOff, const cv::Mat &screenCapture,
                                    const QList<cv::Mat> &matTemplates, const int numTemplates,
                                    double &best, int &bestX, int &bestY, int &bestN)
{
    cv::Mat mat = screenCapture(cv::Rect(rect.x + xOff, rect.y + yOff, rect.width, rect.height));

    for(int i=0; i<numTemplates; i++)
    {
        double l2 = getL2Mat(mat, matTemplates[i]);
        if(l2 < best)
        {
            best = l2;
            bestX = xOff;
            bestY = yOff;
            bestN = i;
        }
    }
}


double DraftHandler::getL2Mat(const cv::Mat &matSample, const cv::Mat &matTemplate)
{
    return (norm(matSample, matTemplate, cv::NORM_L2) / (matSample.rows * matSample.cols));
}


cv::Mat DraftHandler::getScreenMat()
{
    QList<QScreen *> screens = QGuiApplication::screens();
    if(screenIndex >= screens.count() || screenIndex < 0)  return cv::Mat();
    QScreen *screen = screens[screenIndex];
    QImage image = Utility::getScreenshot(screen);
    if(image.isNull())  return cv::Mat();
    cv::Mat mat(image.height(),image.width(),CV_8UC4,image.bits(), static_cast<ulong>(image.bytesPerLine()));
    return mat.clone();
}


void DraftHandler::loadImgTemplates(QList<cv::Mat> &imgTemplates, const QString &filename)
{
    int num;
    int type, rows, cols;
    bool continuous;

    QFile file(Utility::extraPath() + "/" + filename);
    if(!file.open(QIODevice::ReadOnly))
    {
        emit pDebug("ERROR: Cannot open " + file.fileName());
        return;
    }
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_5_5);

    while(!in.atEnd())
    {
        in >> num >> type >> rows >> cols >> continuous;
        cv::Mat mat = cv::Mat(rows, cols, Utility::cvTypeFromFile(type));

        if(continuous)
        {
            size_t const dataSize = rows * cols * mat.elemSize();
            in.readRawData(reinterpret_cast<char*>(mat.ptr()), dataSize);
        }
        else
        {
            size_t const rowSize(cols * mat.elemSize());
            for(int i=0; i<rows; i++)   in.readRawData(reinterpret_cast<char*>(mat.ptr(i)), rowSize);
        }
        imgTemplates += mat;
    }
    file.close();
}


bool DraftHandler::isSignatureCard(const QString &code)
{
    QStringList candidates = {
        //CFM 4
        KAZAKUS, PATCHES_THE_PIRATE, DON_HANCHO, AYA_BLACKPAW,
        //CORE 4
        "CORE_TOY_100", "CORE_TOY_101", "CORE_TOY_102", "CORE_TOY_103",
        //EDR 40
        "EDR_819", "EDR_818", "EDR_421", "EDR_209", "EDR_845", "EDR_226", "EDR_227", "EDR_480", "EDR_430", "EDR_804", "EDR_941", "EDR_258", "EDR_259", "EDR_264", "EDR_451",
        "EDR_449", "EDR_464", "EDR_476", "EDR_895", "EDR_970", "EDR_527", "EDR_031", "EDR_231", "EDR_238", "EDR_518", "EDR_489", "EDR_465", "EDR_471", "EDR_001", "EDR_105",
        "EDR_495", "EDR_800", "EDR_844", "EDR_846", "EDR_852", "EDR_856", "EDR_861", "EDR_888", "EDR_971", "EDR_979",
        //ETC 15
        MC_BLINGTRON, VOID_VIRTUOSO, COWBELL_SOLOIST, HIPSTER, ROCK_MASTER_VOONE, KANGOR_DANCING_KING, HEARTTHROB, INZAH,
        ZOK_FOGSNOUT, DJ_MANASTORM, THE_ONE_AMALGAM_BAND, SNAKEBITE, CAGE_HEAD, TONY_KING_OF_PIRACY, MISTER_MUKLA,
        //EX1 3
        TIRION_FORDRING, CENARIUS, "EX1_002",
        //FIR 2
        "FIR_951", "FIR_958",
        //FP1 1
        LOATHEB,
        //GDB 35
        "GDB_100", "GDB_101", "GDB_102", "GDB_103", "GDB_104", "GDB_105", "GDB_106", "GDB_107", "GDB_108", "GDB_109", "GDB_110", "GDB_111", "GDB_112", "GDB_117", "GDB_118",
        "GDB_126", "GDB_127", "GDB_136", "GDB_141", "GDB_142", "GDB_233", "GDB_235", "GDB_301", "GDB_310", "GDB_341", "GDB_442", "GDB_447", "GDB_466", "GDB_467", "GDB_477",
        "GDB_479", "GDB_842", "GDB_856", "GDB_862", "GDB_876",
        //KAR 1
        "KAR_061",
        //LOE 4
        RENO_JACKSON, SIR_FINLEY_MRRGGLTON, BRANN_BRONZEBEARD, ELISE_STARSEEKER,
        //OG 4
        YSHAARJ_RAGE_UNBOUND, NZOTH_THE_CORRUPTOR, YOGG_SARON_HOPES_END, CTHUN,
        //RLK 18 (Excepcion, gema rareza en sitio correcto)
        //SC 3
        "SC_004", "SC_400", "SC_754",
        //TLC 21
        "TLC_401", "TLC_631", "TLC_841", "TLC_239", "TLC_828", "TLC_830", "TLC_226", "TLC_460", "TLC_241", "TLC_426", "TLC_430", "TLC_811", "TLC_817", "TLC_513", "TLC_519",
        "TLC_522", "TLC_228", "TLC_446", "TLC_602", "TLC_624", "TLC_243",
        //TOY 18
        "TOY_100", "TOY_101", "TOY_102", "TOY_103", "TOY_355", "TOY_356", "TOY_376", "TOY_383", "TOY_504", "TOY_515", "TOY_524", "TOY_531", "TOY_651", "TOY_806", "TOY_812",
        "TOY_821", "TOY_866", "TOY_913",
        //TTN 15
        SIF, IMPRISONED_HORROR, MINOTAUREN, RADEN, LOKEN_JAILER_OF_YOGGSARON, ANGRY_HELHOUND, HODIR_FATHER_OF_GIANTS, RAVENOUS_KRAKEN,
        ODYN_PRIME_DESIGNATE, THORIM_STORMLORD, JOTUN_THE_ETERNAL, TYR, ASTRAL_SERPENT, MIMIRON_THE_MASTERMIND, FREYA_KEEPER_OF_NATURE,
        //VAC 26
        "VAC_304", "VAC_336", "VAC_340", "VAC_407", "VAC_413", "VAC_420", "VAC_424", "VAC_427", "VAC_437", "VAC_450", "VAC_464", "VAC_501", "VAC_503", "VAC_507", "VAC_509",
        "VAC_519", "VAC_523", "VAC_533", "VAC_702", "VAC_915", "VAC_945", "VAC_948", "VAC_955", "VAC_957", "VAC_958", "VAC_959",
        //WON 3
        CTHUN2, AYA_BLACKPAW2, DON_HANCHO2,
        //WORK 2
        TURBULUS, PORTALMANCER_SKYLA,
        //WW 18
        KOBOLD_MINER, SKARR_THE_CATASTROPHE, POPGAR_THE_PUTRID, PILE_OF_BONES, HOWDYFIN, SPIRIT_OF_THE_BADLANDS, SHERIFF_BARRELBRIM,
        RESKA_THE_PIT_BOSS, SLAGMAW_THE_SLUMBERING, WALKING_MOUNTAIN, PIP_THE_POTENT, HIGH_NOON_DUELIST, GUNSLINGER_KURTRUS,
        WISHING_WELL, DRILLY_THE_KID, TAETHELAN_BLOODWATCHER, THELDURIN_THE_LOST, FYE_THE_SETTING_SUN
    };
    if(candidates.contains(code))   return true;
    return false;
}


/*
void DraftHandler::testSave()
{
    //Save
    int type, rows, cols;
    bool continuous;

    QFile file(Utility::extraPath() + "/MANA" + HISTOGRAM_EXT);
    if(!file.open(QIODevice::WriteOnly))
    {
        emit pDebug("ERROR: Cannot open " + file.fileName());
        return;
    }
    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_5_5);

    for(int num=0; num<10; num++)
    {
        cv::Mat mat = cv::imread(("/home/triodo/Documentos/ArenaTracker/Extra/mana" +
                                   QString::number(num) + ".png").toStdString(), cv::IMREAD_UNCHANGED);
        type = Utility::cvTypeToFile(mat.type());
        rows = mat.rows;
        cols = mat.cols;
        continuous = mat.isContinuous();
        out << num << type << rows << cols << continuous;

        if(continuous)
        {
            size_t const dataSize = rows * cols * mat.elemSize();
            out.writeRawData(reinterpret_cast<char const*>(mat.ptr()), dataSize);
        }
        else
        {
            size_t const rowSize(cols * mat.elemSize());
            for(int i=0; i<rows; i++)   out.writeRawData(reinterpret_cast<char const*>(mat.ptr(i)), rowSize);
        }
        qDebug()<<"WRITE:" << num << type << rows << cols << continuous;
    }
    file.close();
}
*/


//Heroes
//01) Warrior
//02) Shaman
//03) Rogue
//04) Paladin
//05) Hunter
//06) Druid
//07) Warlock
//08) Mage
//09) Priest
//10) Demon Hunter
//11) Death Knight
