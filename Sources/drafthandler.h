#ifndef DRAFTHANDLER_H
#define DRAFTHANDLER_H

#include "deckhandler.h"
#include "Cards/draftcard.h"
#include "utility.h"
#include "Widgets/draftherowindow.h"
#include "Widgets/draftscorewindow.h"
#include <QObject>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <atomic>

#define DRAFT_DELAY_TIME        2000
#define HERODRAFT_DELAY_TIME    2000
// #define CONTINUEDRAFT_DELAY_TIME    3000
#define REDRAFT_REVIEW_DELAY_TIME   1500
#define REDRAFT_REMOVE_CARDS        8       //Suggested to the mascot: 5 are discarded, 3 more to choose from
#define REDRAFT_REMOVE_SECTIONS     2
#define REDRAFT_WATCH_TIME          2000
#define REDRAFT_REVIEW_OCR_TIME     1000
#define FINDSCREEN_LOOP_TIME    1000
#define FINDSCREEN_STABLE_TIME  400
#define FINDSCREEN_VERIFY_TIME  1500

#define CAPTUREDRAFT_DELAY_TIME         1500
#define CAPTUREDRAFT_LOOP_TIME          100
#define CAPTUREDRAFT_LOOP_TIME_FADING   200
#define CAPTUREDRAFT_LOOP_TIME_REDRAFT  500
#define CAPTUREDRAFT_RETRY_TIME         500     //After a failed screenshot
#define CAPTUREDRAFT_MAX_FAILS          10      //Failed screenshots in a row before the screen is looked for again
#define HERO_CAPTURE_RESCAN_TIME        8000    //Heroes not read by then: the screen is looked for again

#define CARD_ACCEPTED_THRESHOLD             0.35
#define CARD_ACCEPTED_THRESHOLD_REDRAFT     0.4
#define CARD_ACCEPTED_THRESHOLD_INCREASE    0.02
#define CAPTURE_MIN_CANDIDATES                 7
#define CAPTURE_EXTENDED_CANDIDATES            15

#define MANA_L2_THRESHOLD       4.5
#define RARITY_L2_THRESHOLD     9

#define PREV_CODES_TIME         10

#define HISTOGRAM_EXT                   ".dat"


//The three cards of a pick and their scores, for the mascot's advice
struct PickScores
{
    QString names[3], codes[3];
    float fire[3] = {0, 0, 0};
    int fireGames[3] = {-1, -1, -1};
    float ha[3] = {0, 0, 0};
    bool showFire = false, showHA = false;
    bool legendaryGroup = false;    //The first pick: legendary groups
};


//The cards to remove in a redraft by one score source, for the mascot
struct RedraftSuggestionCard
{
    QString name, score, code;
};
struct RedraftSuggestion
{
    QString source;
    QList<RedraftSuggestionCard> cards;
};


//A big number of the arena screens (the Ready Up medal, the rewards chest): 0-12, or why it wasn't read (BigNumberFail),
//and all the text the OCR read, logged when it's never read
struct BigNumberRead
{
    int wins = -1;
    QString text;
};


//What the OCR of the Hearthstone window found while redraftingReview
enum RedraftScreen { RedraftScreenOther, RedraftScreenDiscard, RedraftScreenReadyUp };
struct RedraftScreenRead
{
    RedraftScreen screen = RedraftScreenOther;
    QStringList codes;      //Cards picked on the discard screen
};


//The screen an arena screen search looks for
enum FindScreenMode { FindDraft, FindHeroes, FindRedraftReview };


class SDBasic
{
public:
    std::vector<Point2f> screenPoints;
    int goodMatches = 0;
    int screenIndex = -1;
    int screenHeight = 1;
    QPointF screenScale = QPointF(0,0);
};


class ScreenDetection
{
public:
    cv::Rect screenRects[5];
    cv::Rect manaRects[5];
    cv::Rect rarityRects[5];
    int screenIndex = -1;
    int screenHeight = 1;
    QPointF screenScale = QPointF(0,0);
};

//Mana/rarity review of one draft slot. Computed in a worker thread and applied
//in the GUI thread, which owns the candidate maps.
class ReviewSlot
{
public:
    QString slotCode;       //Card of the slot when the review started
    QString code;           //Suggested card, empty if the slot card is right
    DraftCard newCard;      //Card to add to the candidates, empty code if it is already there
};

//What the mana/rarity review needs, copied in the GUI thread: the worker thread never reads the handler's members,
//which an end of the draft clears meanwhile (Qt containers and cv::Mat share their data, the copies are cheap)
class ReviewInput
{
public:
    QList<QList<DraftCard>> candidates;
    QList<DraftCard> slotCards;
    cv::Rect screenRects[3], manaRects[3], rarityRects[3];
    int screenIndex = -1;
    QList<cv::Mat> manaTemplates, rarityTemplates;
    QMap<QString, cv::MatND> cardsHist;
    CardClass arenaHero = INVALID_CLASS, arenaHeroMulticlassPower = INVALID_CLASS;
    bool multiclassArena = false;
};


class DraftHandler : public QObject
{
    Q_OBJECT
public:
    DraftHandler(QObject *parent, DeckHandler *deckHandler);
    ~DraftHandler();

//Variables
private:
    DeckHandler *deckHandler;
    QString draftStatus;    //What the draft recognition is doing, said by the mascot
    int findScreenFails;
    //Deck cards suggested for removal after a redraft, worst first: section 0 by Firestone, 1 by HearthArena
    QList<DeckCard> redraftRemoveCards[REDRAFT_REMOVE_SECTIONS];
    QMap<QString, int> hearthArenaTiers;
    QMap<QString, int> lightForgeTiers;
    //Guarda los codes en la rotacion. Parte de todos los arena sets o se limita a la tier list de HA (si trustHA)
    QMap<CardClass, QStringList> codesByClass;
    QMap<QString, cv::MatND> cardsHist;
    QStringList cardsDownloading;
    DraftCard draftCards[3];
    //Guarda los mejores candidatos de esta iteracion
    QMap<QString, DraftCard> draftCardMaps[3];  //[Code(_premium)] --> DraftCard
    //Se crea al final de la iteracion para ordenar los candidatos por match score
    QMultiMap<double, QString> bestMatchesMaps[3];   //[Match] --> Code(_premium)
    bool cardDetected[3];
    CardClass arenaHero, arenaHeroMulticlassPower;
    int numDraftedCards = 0;    //Cards of the deck, the ones it had when the draft started included
    int deckRatingHA;
    float deckRatingFire;
    cv::Rect screenRects[5];
    cv::Rect manaRects[5];
    cv::Rect rarityRects[5];
    QPointF screenScale;
    int screenIndex;
    int numCaptured;
    int captureFails = 0;                 //Screenshots failed in a row by the capture loop
    QElapsedTimer heroCaptureClock;        //From the start of the hero capture loop to the heroes read
    bool drafting, heroDrafting, redrafting, redraftingReview, findingFrame, stopLoops;
    std::atomic<bool> capturing;    //Also read by the mana/rarity review's worker thread
    bool heroesShown = false;   //The current heroes are scored (heroesScored)
    bool redraftPicksSeen = false;  //OCR read card names on the redraft's pick screen: REDRAFTING alone only offers it
    bool bundlePreviewOpen = false; //A legendary group's preview covers the cards: no capture
    DraftHeroWindow *draftHeroWindow;
    DraftScoreWindow *draftScoreWindow;
    bool showDraftScoresOverlay;
    bool showMyWR;
    QString justPickedCard; //Evita doble pick card en Arena.log
    bool draftMethodHA, draftMethodFire;
    QFutureWatcher<ScreenDetection> futureFindScreenRects;
    FindScreenMode findScreenRunMode = FindDraft;  //What the running search looks for
    QElapsedTimer findScreenClock;         //From the start of findScreenRects to its result
    std::atomic<qint64> findScreenStartMs{0}, findScreenCaptureMs{0};
    bool extendedCapture;
    QStringList heroCodesList;
    QMap<QString, float> *fireWRMap;
    QMap<QString, int> *fireSamplesMap;
    bool multiclassArena;
    bool needSaveCardHist;
    int cardsJsonWaits;
    ScreenDetection prevScreenDetection;    //Last detection, to wait for a stable screen
    QMap<QString, QString> cardsNameMap;    //Name -> code, to match the card names read by OCR
    QFutureWatcher<QList<ReviewSlot>> futureReviewBestCards;
    //Looks for the redraft review screen ("35/30" deck counter) while in the arena menu, in case
    //the redraft picks happened when AT could not see them (Hearthstone restarted in the review screen)
    QTimer *redraftWatchTimer;
    QFutureWatcher<int> futureRedraftCounter;
    QFutureWatcher<BigNumberRead> futureRewardsWins;
    int rewardsWinsTries = 0;
    int rewardsWinsWaits = 0;   //Tries put off while Hearthstone isn't on screen
    QFutureWatcher<BigNumberRead> futureReadyUpWins;
    QImage bigNumberImage;      //The last screen read for a big number
    int readyUpWinsTries = 0;
    int readyUpWinsWaits = 0;
    //macOS: the cards picked in the redraft review screen are found by reading their names
    QTimer *redraftReviewTimer;
    QFutureWatcher<RedraftScreenRead> futureRedraftReviewCodes;
    RedraftScreen redraftScreen = RedraftScreenOther;     //Last screen read while redraftingReview
    PickScores pickScores;
    int redraftScreenMisses = 0;    //Reads in a row that didn't find the discard screen while on it
    bool redraftDiscardSeen = false;    //The discard screen was on screen in this review: gone means Done was pressed
    //Legendary bundles (macOS). The log only names the previewed legendary, when its preview opens;
    //the bundle cards are read from the preview, or else from the deck list once it is picked.
    QTimer *bundleTimer;
    QFutureWatcher<QPair<bool, QStringList>> futureBundle;
    QFutureWatcher<QStringList> futureDeckList;
    QString bundleLegendary;
    QMap<QString, QStringList> bundlePreviews;  //Legendary -> its bundle cards, as read
    QMap<QString, QString> bundleNameMap;       //All collectible cards of the draft classes: bundles bring cards from outside the arena sets
    int bundleReads;
    bool bundlePending, bundlePreviewVisible;
    int bundleMisses;
    QMap<QString, QString> redraftNameMap;  //Normalized local name -> code, of the deck cards
    QList<cv::Mat> manaTemplates;
    QList<cv::Mat> rarityTemplates;
    QString prevCodes[3];
    QString ocrCodes[3];    //Card of each slot read by its name (OCR, macOS only)
    QString ocrUnmatchedText[3];    //Last OCR reading that matched no card, logged once
    QString ocrNameCodes[3];        //Card whose name line is at ocrNameCenters (screen capture pixels)
    QPointF ocrNameCenters[3];
    double plateScale = 0;          //Real card spacing / found art spacing, from the names read (0: not measured)
    qint64 prevCodesTime;
    QString bestCodesRedraftingReview[5];


//Metodos
private:
    cv::MatND getHist(const QString &code);
    cv::MatND getHist(const Mat &srcBase);
    void initDeckCounters(QList<DeckCard> &deckCardList);
    void initCodesAndHistMaps(QList<DeckCard> &deckCardList, bool skipScreenSettings=false);
    void clearLists(bool keepCounters);
    void endDraft(bool createNewArena);
    bool getScreenCardsHist(cv::MatND screenCardsHist[], int length);
    void showNewCards(DraftCard bestCards[]);
    void updateDeckScore(float cardRatingHA, float cardRatingFire);
    bool screenFound();
    ScreenDetection findScreenRects(QList<QRect> trackerWindows, FindScreenMode mode);
    FindScreenMode findScreenMode();
    bool findHeroRectsByOcr(ScreenDetection &screenDetection, const QList<QRect> &trackerWindows);
    void deleteDraftHeroWindow();
    void deleteDraftScoreWindow();
    void showOverlay();
    void newCaptureDraftLoop(bool delayed=false);
    void initHearthArenaTiers(const CardClass heroClass, const bool multiClassDraft);
    void initLightForgeTiers(const CardClass heroClass, const bool multiClassDraft, const QStringList &arenaCodes, bool buildCodesByClass);
    void createDraftWindows();
    void mapBestMatchingCodes(cv::MatND screenCardsHist[]);
    double getMinMatch(const QMap<QString, DraftCard> &draftCardMaps);
    bool areCardsDetected();
    bool isRepeatHero();
    void buildBestMatchesMaps();
    void removeDuplicatedPicks(QStringList slotCodes[3]);
    void readCardNames(const cv::Mat &screenCapture);
    void readHeroClasses(const cv::Mat &screenCapture);
    //partial: the text can be any part of the name (the curved names of the legendary groups screen)
    static QString matchCardName(const QStringList &lines, const QMap<QString, QString> &nameMap, bool partial=false);
    void applyOcrCodes(QStringList slotCodes[3]);
    void getBestCards(DraftCard bestCards[3]);
    void addCardHist(QString code, bool premium);
    QString degoldCode(QString fileName);
    bool isGoldCode(QString fileName);
    void endHeroDraft();
    void showNewHeroes();
    bool loadTemplateSettings();
    bool saveTemplateSettings();
    bool isFindScreenOk(ScreenDetection &screenDetection);
    bool isFindScreenStable(ScreenDetection &screenDetection);
    bool isFindScreenAsSettings(ScreenDetection &screenDetection);
    void refreshHeroes();
    void refreshCapturedCards();
    void processCardHist(QStringList &codes);
    bool initCardHist();
    void loadCardHist(QString classUName);
    void saveCardHist();
    CardClass findMulticlassPower(QList<DeckCard> &deckCardList);
    void initCardsNameMap();
    void reduceCardsNameMapMulticlass();
    void getBestNManaRarity(int &manaN, CardRarity &cardRarity, const cv::Mat &screenSmall, const QList<Mat> &manaTemplates, const QList<Mat> &rarityTemplates,
                            const cv::Rect &manaRectSmall, const cv::Rect &rarityRectSmall);
    double getL2Mat(const cv::Mat &matSample, const cv::Mat &matTemplate);
    void getBestNOnRect(const cv::Rect &rect, const int xOff, const int yOff, const cv::Mat &screenCapture,
                                const QList<Mat> &matTemplates, const int numTemplates, double &best, int &bestX, int &bestY, int &bestN);
    void setStartEndLoop(int &startX, int &startY, int &endX, int &endY, const int centerX, const int centerY, const int jump);
    void getBestN(int &bestNs, double &bestL2s, const Rect &rectSmall, const cv::Mat &screenCapture, const QList<Mat> &matTemplates, const int numTemplates);
    cv::Mat getScreenMat();
    static cv::Mat getScreenMat(int screenIndex);
    ReviewSlot getBestMatchManaRarity(const ReviewInput &input, const int pos, const Mat &screenBig,
                                      const int imgMana, const CardRarity imgRarity);
    DraftCard getBestAllMatchManaRarity(const ReviewInput &input, const MatND &screenCardHist, const int imgMana, const CardRarity imgRarity);
    void startReviewBestCards();
    QList<ReviewSlot> reviewBestCards(ReviewInput input);
    void loadImgTemplates(QList<Mat> &imgTemplates, const QString &filename);
    bool areScreenRectsValid(Mat &screenCapture, int length);
    bool isSignatureCard(const QString &code);
    void endRedraftReview();
    void captureDraftRedraftingReview();
    void setRedraftReviewCodes(const QStringList &codes);
    void startBundlePreview(const QString &code);
    void confirmBundle();
    void checkScreenAgain(const QString &reason);
    void resetBundle();
    void readDeckList();
    void buildBundleNameMap();
    void showDraftNotice(const QString &text);
    void logUnreadBigNumber(const QString &what, const BigNumberRead &read);
    void beginRedraftReview();
    void emitDraftFinished();
    void updatePickRatingPool();
    bool isPickShown();
    bool isRedraftOffered();
    void setDraftStatus(const QString &text);
    void updateRedraftRemoveList();
    void fillRedraftRemoveSection(int section, DraftMethod draftMethod);
    void clearRedraftRemoveList();
    void startRedraftWatch();
    void stopRedraftWatch();
    bool areScreenPointsValid(std::vector<Point2f> screenPoints, int screenHeight);
    void showHAScores(QString ogCodes[], QString cardNames[]);
    void showFireScores(QString ogCodes[], QString cardNames[]);
    QString getFireCode(QString code);
    void addLFCode(const QString &code, const CardClass &heroClass, const bool multiClassDraft, bool buildCodesByClass);
    int getHAScore(const QString &code);

public:
    QString getHACode(QString code);
    QString getFireCode(QString code, CardClass heroClass);
    void setDeckScores();
    QList<RedraftSuggestion> getRedraftRemoveSuggestions();
    void rescan();
    PickScores getPickScores();
    void buildHeroCodesList();
    void reHistDownloadedCardImage(const QString &fileNameCode, bool missingOnWeb=false);
    void setShowDraftScoresOverlay(bool value);
    void setDraftMethod(bool draftMethodHA, bool draftMethodFire);
    bool isDrafting();
    bool isRedrafting();
    void readRewardsWins();
    void readReadyUpWins();
    bool isEmptyDeck();
    QString getDraftStatus();
    void setFireWRMap(QMap<QString, float> fireWRMap[]);
    void setFireSamplesMap(QMap<QString, int> fireSamplesMap[]);
    void setMulticlassArena(bool multiclassArena);
    void closeFindScreenRects();
    CardClass getArenaHero();
    void initTierLists(const CardClass &heroClass);
    void setShowMyWR(bool value);

signals:
    void draftStatusChanged(QString text);
    void redraftScreenChanged(int screen);      //RedraftScreen
    void rewardsWinsRead(int wins);             //The number on the rewards chest, -1 when it couldn't be read
    void readyUpWinsRead(int wins);             //The wins on the Ready Up medal
    void draftFinished(int knownCards, float avgFire, float avgHA);   //A new deck (not a redraft), for the mascot
    void heroesScored(int classOrder0, int classOrder1, int classOrder2);
    void cardsScored();     //getPickScores() has the new pick
    void checkCardImage(QString code);
    void newDeckCard(QString code);
    void draftStarted();
    void draftEnded(QString heroLog);
    void saveDraftDeck(QString heroLog);
    void deleteDraftDeck(QString heroLog);
    void scoreAvg(int deckScoreHA, float deckScoreFire, QString heroLog);
    void overlayCardEntered(QString code, QRect rectCard, int maxTop, int maxBottom, bool alignReverse=true);
    void overlayCardLeave();
    void pDebug(QString line, DebugLevel debugLevel=Normal, QString file="DraftHandler");

public slots:
    void updateHeroScores();
    void beginDraft(QString hero, QList<DeckCard> deckCardList = QList<DeckCard>(), bool skipScreenSettings=false);
    void continueDraft();
    void beginHeroDraft();
    void heroDraftDeck(QString hero);
    void activeDraftDeck();
    void stopDraft();
    void showNewRatings(const QString &cardName1, const QString &cardName2, const QString &cardName3, float rating1, float rating2, float rating3,
                        DraftMethod draftMethod,
                        int includedDecks1=-1, int includedDecks2=-1, int includedDecks3=-1);
    void pickCard(QString code);
    // void enterArena();//OLD
    void leaveArena();
    void redraft();
    void checkRedraft();

private slots:
    void tryReadRewardsWins();
    void tryReadReadyUpWins();
    void finishReadReadyUpWins();
    void finishReadRewardsWins();
    void captureDraft();
    void finishFindScreenRects();
    void startFindScreenRects();
    void refreshDraft();
    void newFindScreenLoop(bool skipScreenSettings=false);
    void finishReviewBestCards();
    void checkRedraftScreen();
    void finishCheckRedraftScreen();
    void captureRedraftReviewNames();
    void captureBundlePreview();
    void finishBundlePreview();
    void finishDeckList();
    void finishRedraftReviewNames();
};

#endif // DRAFTHANDLER_H
