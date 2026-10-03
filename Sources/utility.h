#ifndef UTILITY_H
#define UTILITY_H

#include "opencv2/opencv.hpp"
#include "Utils/libzippp.h"
#include <QString>
#include <QLayout>
#include <QMap>
#include <QJsonObject>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QScreen>
#include <QPropertyAnimation>


//Repo the app downloads its data from (cards, arena sets, tier lists, images...)
#define AT_REPO_RAW_URL "https://raw.githubusercontent.com/arenadude/arenadude/master"

#define REMOVE_CARDS_ON_VERSION_UPDATE false
#define REMOVE_EXTRA_AND_HISTOGRAMS_ON_VERSION_UPDATE false

#define DEBUG_OVERLAYS_LEFT false
#define DEBUG_OVERLAYS_RIGHT false
#define DEBUG_ALLOW_SAME_TRIO false


using namespace cv;

#define FLOATEQ(X, Y)  (fabs(X - Y) < 0.000001f)

enum DebugLevel { Normal, Warning, Error };
enum LoadingScreenState { menu, arena, ranked, adventure, tavernBrawl, friendly, gameMode, unknown };
enum LogComponent { logLoadingScreen, logArena, logPower, logZone, logAsset, logInvalid };
enum DraftMethod { HearthArena, FireStone, None };


//DeckCard
//Usamos sus numeros para comparacion con rarity template en DraftHandler::reviewBestCards()
enum CardRarity {COMMON=0, RARE=1, EPIC=2, LEGENDARY=3, FREE, INVALID_RARITY};
enum CardType {INVALID_TYPE, HERO, MINION, SPELL, WEAPON, HERO_POWER, LOCATION};
//--------------------------------------------------------
//----NEW HERO CLASS - Orden alfabetico
//--------------------------------------------------------
enum CardClass {DEATHKNIGHT, DEMONHUNTER, DRUID, HUNTER, MAGE, PALADIN, PRIEST, ROGUE, SHAMAN, WARLOCK, WARRIOR,
                 NUM_HEROS, INVALID_CLASS, NEUTRAL};


//Draft overlays (scores, heroes, mechanics): non-activating panels on macOS (made so by MacFullScreenOverlay), the only
//windows that get into Hearthstone's fullscreen Space whenever they are created. Also set Qt::WA_MacAlwaysShowToolWindow.
#define OVERLAY_WINDOW_FLAGS (Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::NoDropShadowWindowHint)


//SIFT features of a screenshot, computed once and matched against several templates
struct SceneFeatures
{
    cv::Mat gray;
    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
};

//The features of a screen, found on a downscaled screenshot (scale = downscaled / original)
struct ScreenFeatures
{
    SceneFeatures scene;
    double scale = 1.0;
    QPointF screenScale = QPointF(0,0);
    int screenHeight = 1;
    bool valid = false;
};


class Utility
{
//Constructor
public:
    Utility();
    ~Utility();

//Variables
private:
    static QMap<QString, QJsonObject> *cardsJson;
    static QString localLang;
    static QString diacriticLetters;
    static QStringList noDiacriticLetters;
    static bool trustHA;
    static bool cardsJsonUpToDate;
    static QStringList arenaSets;


//Metodos
private:
    static CardClass classString2cardClass(const QString &value);

public:
    static QString className2classLogNumber(const QString &hero);
    static QString classEnum2classLogNumber(CardClass cardClass);
    static QString classEnum2classUName(CardClass cardClass);
    static CardClass classLogNumber2classEnum(const QString &hero);
    static QString classOrder2classULName(int order);
    static QString classOrder2classLName(int order);
    static QString classOrder2classUL_ULName(int order);
    static QString classOrder2classLogNumber(int order);
    static QJsonValue getCardAttribute(const QString &code, const QString &attribute);
    static QString appPath();
    static QString dataPath();
    static void migrateFromArenaTracker();
    static QString hscardsPath();
    static QString extraPath();
    static QString cardEnNameFromCode(const QString &code);
    static QString cardLocalNameFromCode(const QString &code);
    static QStringList cardEnCodesFromName(const QString &name, bool onlyCollectible=true);
    static void setCardsJson(QMap<QString, QJsonObject> *cardsJson);
    static void setLocalLang(const QString &localLang);
    static QString getLocalLang();
    static QString removeAccents(const QString &s);
    static QImage getScreenshot(QScreen *screen);
    static SceneFeatures sceneFeatures(const cv::Mat &mat);
    static ScreenFeatures screenFeatures(QScreen *screen, QImage image);
    static std::vector<Point2f> findTemplateOnScreen(const QString &templateImage, const ScreenFeatures &screen,
                                                     const std::vector<Point2f> &templatePoints, int &goodMatches);
    static ulong findTemplateOnMat(const QString &templateImage, cv::Mat &mat);
    static ulong findTemplateOnMat(const QString &templateImage, Mat &mat, const std::vector<Point2f> &templatePoints,
                                   std::vector<Point2f> &targetPoints, ulong minGoodMatches);
    static ulong findTemplateOnScene(const QString &templateImage, const SceneFeatures &scene, const std::vector<Point2f> &templatePoints,
                                     std::vector<Point2f> &targetPoints, ulong minGoodMatches);
    static bool isLeftOfScreen(QPoint center);
    static CardType getTypeFromCode(const QString &code);
    static CardRarity getRarityFromCode(const QString &code);
    static QList<CardClass> getClassFromCode(const QString &code);
    static void dumpOnFile(const QByteArray &data, const QString &path);
    static QString histogramsPath();
    static QString arenaStatsPath();
    static int classLogNumber2classOrder(const QString &heroLog);
    static void clearLayout(QLayout *layout, bool deleteWidgets, bool recursive);
    static void showItemsLayout(QLayout *layout);
    static QStringList getSetCodes(const QString &set, bool excludeHeroes, bool onlyCollectible);
    static QStringList getWildCodes();
    static QStringList getAllArenaCodes();
    static QStringList getAllArenaCodes(bool trustHA);
    static QJsonObject loadHearthArena();
    static void setTrustHA(bool trustHA);
    static bool isCardsJsonUpToDate();
    static void setCardsJsonUpToDate(bool upToDate);
    static void setArenaSets(QStringList arenaSets);
    static int cvTypeFromFile(int fileType);
    static int cvTypeToFile(int type);
    static bool createDir(const QString &pathDir);
    static void unZip(const QString &zipName, const QString &targetPath);
    static int className2classOrder(const QString &className);
    static bool needCodesSpecific(const QString &set);
    static QStringList getSetCodesSpecific(const QString &set);
};

#endif // UTILITY_H
