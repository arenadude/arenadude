#include "utility.h"
#include "constants.h"
#include "Utils/replayscreen.h"
#include "Utils/macscreen.h"
#include <atomic>
#include <QtWidgets>
#include "opencv2/features2d.hpp"
#include "opencv2/calib3d.hpp"
#include "opencv2/highgui.hpp"


using namespace libzippp;
using namespace std;


QMap<QString, QJsonObject> * Utility::cardsJson = nullptr;
QString Utility::localLang = "enUS";
QString Utility::diacriticLetters;
QStringList Utility::noDiacriticLetters;
bool Utility::trustHA;
bool Utility::cardsJsonUpToDate = false;
QStringList Utility::arenaSets;

Utility::Utility()
{
}

Utility::~Utility()
{
}


//--------------------------------------------------------
//----Conversiones de clases
//----NEW HERO CLASS
//--------------------------------------------------------
QString Utility::className2classLogNumber(const QString &hero)
{
    const QString heroL = hero.toLower();
    if(heroL.compare("druid")==0)           return QString("06");
    else if(heroL.compare("hunter")==0)     return QString("05");
    else if(heroL.compare("mage")==0)       return QString("08");
    else if(heroL.compare("paladin")==0)    return QString("04");
    else if(heroL.compare("priest")==0)     return QString("09");
    else if(heroL.compare("rogue")==0)      return QString("03");
    else if(heroL.compare("shaman")==0)     return QString("02");
    else if(heroL.compare("warlock")==0)    return QString("07");
    else if(heroL.compare("warrior")==0)    return QString("01");
    else if(heroL.compare("demonhunter")==0) return QString("10");
    else if(heroL.compare("deathknight")==0) return QString("11");
    else return QString();
}


QString Utility::classEnum2classLogNumber(CardClass cardClass)
{
    if(cardClass == DRUID)        return QString("06");
    else if(cardClass == HUNTER)  return QString("05");
    else if(cardClass == MAGE)    return QString("08");
    else if(cardClass == PALADIN) return QString("04");
    else if(cardClass == PRIEST)  return QString("09");
    else if(cardClass == ROGUE)   return QString("03");
    else if(cardClass == SHAMAN)  return QString("02");
    else if(cardClass == WARLOCK) return QString("07");
    else if(cardClass == WARRIOR) return QString("01");
    else if(cardClass == DEMONHUNTER) return QString("10");
    else if(cardClass == DEATHKNIGHT) return QString("11");
    else return QString();
}


QString Utility::classEnum2classUName(CardClass cardClass)
{
    if(cardClass == DRUID)        return QString("DRUID");
    else if(cardClass == HUNTER)  return QString("HUNTER");
    else if(cardClass == MAGE)    return QString("MAGE");
    else if(cardClass == PALADIN) return QString("PALADIN");
    else if(cardClass == PRIEST)  return QString("PRIEST");
    else if(cardClass == ROGUE)   return QString("ROGUE");
    else if(cardClass == SHAMAN)  return QString("SHAMAN");
    else if(cardClass == WARLOCK) return QString("WARLOCK");
    else if(cardClass == WARRIOR) return QString("WARRIOR");
    else if(cardClass == DEMONHUNTER) return QString("DEMONHUNTER");
    else if(cardClass == DEATHKNIGHT) return QString("DEATHKNIGHT");
    else if(cardClass == NEUTRAL) return QString("NEUTRAL");
    else return QString();
}


CardClass Utility::classLogNumber2classEnum(const QString &hero)
{
    if(hero == QString("06"))       return DRUID;
    else if(hero == QString("05"))  return HUNTER;
    else if(hero == QString("08"))  return MAGE;
    else if(hero == QString("04"))  return PALADIN;
    else if(hero == QString("09"))  return PRIEST;
    else if(hero == QString("03"))  return ROGUE;
    else if(hero == QString("02"))  return SHAMAN;
    else if(hero == QString("07"))  return WARLOCK;
    else if(hero == QString("01"))  return WARRIOR;
    else if(hero == QString("10"))  return DEMONHUNTER;
    else if(hero == QString("11"))  return DEATHKNIGHT;
    else                            return INVALID_CLASS;
}


QString Utility::classOrder2classULName(int order)
{
    QString heroes[NUM_HEROS] = {"Deathknight", "Demonhunter", "Druid", "Hunter", "Mage", "Paladin", "Priest", "Rogue", "Shaman", "Warlock", "Warrior"};
    if(order < 0 || order > (NUM_HEROS-1))  return "";
    return heroes[order];
}


QString Utility::classOrder2classLName(int order)
{
    return Utility::classOrder2classULName(order).toLower();
}


QString Utility::classOrder2classUL_ULName(int order)
{
    QString heroes[NUM_HEROS] = {"Death Knight", "Demon Hunter", "Druid", "Hunter", "Mage", "Paladin", "Priest", "Rogue", "Shaman", "Warlock", "Warrior"};
    if(order < 0 || order > (NUM_HEROS-1))  return "";
    return heroes[order];
}


QString Utility::classOrder2classLogNumber(int order)
{
    QString heroesLogNumber[NUM_HEROS] = {"11", "10", "06", "05", "08", "04", "09", "03", "02", "07", "01"};
    if(order < 0 || order > (NUM_HEROS-1))  return "";
    return heroesLogNumber[order];
}


int Utility::classLogNumber2classOrder(const QString &heroLog)
{
    int heroeOrder[NUM_HEROS] = {10, 8, 7, 5, 3, 2, 9, 4, 6, 1, 0};
    int heroLogInt = heroLog.toInt() - 1;
    if(heroLogInt < 0 || heroLogInt > (NUM_HEROS-1))    return -1;
    return heroeOrder[heroLogInt];
}


int Utility::className2classOrder(const QString &className)
{
    QString logNumber = Utility::className2classLogNumber(className);
    return classLogNumber2classOrder(logNumber);
}


QString Utility::cardEnNameFromCode(const QString &code)
{
    return (*cardsJson)[code].value("name").toObject().value("enUS").toString();
}


//cards.json only has the English names (the other languages made it 75 MB): any other language falls back to them
QString Utility::cardLocalNameFromCode(const QString &code)
{
    return getCardAttribute(code, "name").toString();
}


QStringList Utility::cardEnCodesFromName(const QString &name, bool onlyCollectible)
{
    QStringList codes;
    for (QMap<QString, QJsonObject>::const_iterator it = cardsJson->cbegin(); it != cardsJson->cend(); it++)
    {
        if(it->value("name").toObject().value("enUS").toString() == name)
        {
            if(!onlyCollectible || ((it->value("collectible").toBool()) && (!it.key().startsWith("HERO_"))))
            {
                codes << it.key();
            }
        }
    }

    return codes;
}


QJsonValue Utility::getCardAttribute(const QString &code, const QString &attribute)
{
    if(attribute == "text" || attribute == "name")
    {
        const QJsonObject texts = (*cardsJson)[code].value(attribute).toObject();
        return texts.contains(localLang) ? texts.value(localLang) : texts.value("enUS");
    }
    else
    {
        return (*cardsJson)[code].value(attribute);
    }
}


CardType Utility::getTypeFromCode(const QString &code)
{
    QString value = Utility::getCardAttribute(code, "type").toString();
    if(value == "MINION")           return MINION;
    else if(value == "SPELL")       return SPELL;
    else if(value == "WEAPON")      return WEAPON;
    else if(value == "HERO")        return HERO;
    else if(value == "HERO_POWER")  return HERO_POWER;
    else if(value == "LOCATION")    return LOCATION;
    else                            return INVALID_TYPE;
}


CardRarity Utility::getRarityFromCode(const QString &code)
{
    QString value = Utility::getCardAttribute(code, "rarity").toString();
    if(value == "FREE")             return FREE;
    else if(value == "COMMON")      return COMMON;
    else if(value == "RARE")        return RARE;
    else if(value == "EPIC")        return EPIC;
    else if(value == "LEGENDARY")   return LEGENDARY;
    else                            return INVALID_RARITY;
}


//--------------------------------------------------------
//----NEW HERO CLASS
//--------------------------------------------------------
QList<CardClass> Utility::getClassFromCode(const QString &code)
{
    QJsonValue jsonVclasses = Utility::getCardAttribute(code, "classes");
    if(jsonVclasses.isUndefined() || !jsonVclasses.isArray())
    {
        QString stringCardClass = Utility::getCardAttribute(code, "cardClass").toString();
        return {classString2cardClass(stringCardClass)};
    }
    else
    {
        QList<CardClass> cardClassList;
        for(const QJsonValue &jsonVclass: (const QJsonArray)jsonVclasses.toArray())
        {
            cardClassList << classString2cardClass(jsonVclass.toString());
        }
        return cardClassList;
    }
}


CardClass Utility::classString2cardClass(const QString &value)
{
    if(value == "NEUTRAL")      return NEUTRAL;
    else if(value == "DEATHKNIGHT") return DEATHKNIGHT;
    else if(value == "DEMONHUNTER") return DEMONHUNTER;
    else if(value == "DRUID")   return DRUID;
    else if(value == "HUNTER")  return HUNTER;
    else if(value == "MAGE")    return MAGE;
    else if(value == "PALADIN") return PALADIN;
    else if(value == "PRIEST")  return PRIEST;
    else if(value == "ROGUE")   return ROGUE;
    else if(value == "SHAMAN")  return SHAMAN;
    else if(value == "WARLOCK") return WARLOCK;
    else if(value == "WARRIOR") return WARRIOR;
    else                        return NEUTRAL;
}


/*
 * Core "CORE"
 * Legacy "LEGACY" // Old Basic
 * Expert "EXPERT1"// Old Classic
 * Demon Hunter Initiate "DEMON_HUNTER_INITIATE"
 * Taverns of Time "TAVERNS_OF_TIME"
 * Caverns of Time "WONDERS"
 * Duel Treasures "TREASURES"
 * Curse of Naxxramas "NAXX"
 * Goblins vs Gnomes "GVG"
 * Blackrock Mountain "BRM"
 * The Grand Tournament "TGT"
 * League of Explorer "LOE"
 * Whispers of the Old Gods "OG"
 * One Night in Karazhan "KARA"
 * Mean Streets of Gadgetzan "GANGS"
 * Journey to Un'Goro "UNGORO"
 * Knights of the Frozen Throne "ICECROWN"
 * Kobolds and Catacombs "LOOTAPALOOZA"
 * The Witchwood "GILNEAS"
 * The boomsday Project "BOOMSDAY"
 * Rastakhan's Rumble "TROLL"
 * Rise of Shadows "DALARAN"
 * Saviors of Uldum "ULDUM"
 * Descent of Dragons "DRAGONS"
 * Galakrond's Awakening "YEAR_OF_THE_DRAGON"
 * Ashes of Outland "BLACK_TEMPLE"
 * Scholomance Academy "SCHOLOMANCE"
 * Madness at the Darkmoon Faire "DARKMOON_FAIRE"
 * Forged in the Barrens "THE_BARRENS"
 * United in Stormwind "STORMWIND"
 * Fractured in Alterac Valley "ALTERAC_VALLEY"
 * Voyage to the Sunken City "THE_SUNKEN_CITY"
 * Murder at Castle Nathria "REVENDRETH"
 * Path of Arthas "PATH_OF_ARTHAS"
 * March of the Lick King "RETURN_OF_THE_LICH_KING"
 * Festival of Legends "BATTLE_OF_THE_BANDS"
 * TITANS "TITANS"
 * Showdown in the Badlands "WILD_WEST"
 * 10-year anniversary celebration "EVENT"
 * Whizbang’s Workshop "WHIZBANGS_WORKSHOP"
 * Perils in Paradise "ISLAND_VACATION"
 * The Great Dark Beyond "SPACE"
 * Into the Emerald Dream "EMERALD_DREAM"
 * The Lost City of Un’Goro "THE_LOST_CITY"
 * Across the Timeways "TIME_TRAVEL"
 */


QStringList Utility::getSetCodes(const QString &set, bool excludeHeroes, bool onlyCollectible)
{
    QStringList setCodes;
    const QList<QString> codeList = Utility::cardsJson->keys();
    for(const QString &code: codeList)
    {
        if(getCardAttribute(code, "set").toString() == set)
        {
            if  (
                (!onlyCollectible || getCardAttribute(code, "collectible").toBool()) &&
                (!excludeHeroes || !(code.startsWith("HERO_0") || code.startsWith("HERO_1")))
                )
            {
                setCodes.append(code);
            }
        }
    }
    return setCodes;
}


QStringList Utility::getWildCodes()
{
    QStringList setCodes;
    const QList<QString> codeList = Utility::cardsJson->keys();
    for(const QString &code: codeList)
    {
        if(getCardAttribute(code, "collectible").toBool() == true)
        {
            setCodes.append(code);
        }
    }
    return setCodes;
}


QStringList Utility::getAllArenaCodes()
{
    return getAllArenaCodes(Utility::trustHA);
}
QStringList Utility::getAllArenaCodes(bool trustHA)
{
    QStringList codeList;

    if(trustHA)
    {
        QJsonObject jsonObj = loadHearthArena();
        for(int i=0; i<NUM_HEROS; i++)
        {
            const QJsonObject &classCodes = jsonObj.value(Utility::classOrder2classUL_ULName(i)).toObject();
            codeList << classCodes.keys();
        }
    }
    else
    {
        for(const QString &set: qAsConst(arenaSets))
        {
            if(Utility::needCodesSpecific(set)) codeList.append(Utility::getSetCodesSpecific(set));
            else                                codeList.append(Utility::getSetCodes(set, true, true));
        }
    }

    codeList.removeDuplicates();
    return codeList;
}


QJsonObject Utility::loadHearthArena()
{
    QFile jsonFile(Utility::extraPath() + "/hearthArena.json");
    jsonFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonFile.readAll());
    jsonFile.close();
    return jsonDoc.object();
}


//The local cards.json can be outdated until cardsVersion.json is checked (and the new one downloaded).
bool Utility::isCardsJsonUpToDate()
{
    return Utility::cardsJsonUpToDate;
}


void Utility::setCardsJsonUpToDate(bool upToDate)
{
    Utility::cardsJsonUpToDate = upToDate;
}


void Utility::setTrustHA(bool trustHA)
{
    Utility::trustHA = trustHA;
}


void Utility::setArenaSets(QStringList arenaSets)
{
    Utility::arenaSets = arenaSets;
}


//The .dat files (MANA, RARITY, Histograms) store the cv::Mat type with the OpenCV <= 4 encoding (CV_CN_SHIFT 3).
//OpenCV 5 changed CV_CN_SHIFT to 5, so translate on read/write to keep the file format.
int Utility::cvTypeFromFile(int fileType)
{
    return CV_MAKETYPE(fileType & 7, (fileType >> 3) + 1);
}


int Utility::cvTypeToFile(int type)
{
    return CV_MAT_DEPTH(type) + ((CV_MAT_CN(type) - 1) << 3);
}


QString Utility::appPath()
{
    QString dirPath = QCoreApplication::applicationDirPath();

    QDir dir(dirPath);
    dir.cdUp();
    dir.cdUp();
    dir.cdUp();
    return dir.absolutePath();
}


QString Utility::dataPath()
{
    QFileInfo dirInfo(appPath() + "/Arena Dude");
    if(dirInfo.exists())   return dirInfo.absoluteFilePath();
    else
    {
        return QDir::homePath() + "/Arena Dude";
    }
}


//The app was called Arena Tracker: its data dir and its settings move to the new names once
void Utility::migrateFromArenaTracker()
{
    QDir home = QDir::home();
    if(!home.exists("Arena Dude") && home.exists("Arena Tracker") && home.rename("Arena Tracker", "Arena Dude"))
    {
        QDir data(home.filePath("Arena Dude"));
        data.rename("ArenaTrackerDrafts.json", "ArenaDudeDrafts.json");
        data.rename("ArenaTrackerLog.txt", "ArenaDudeLog.txt");     //createLogFile() makes it the .old
        data.remove("ArenaTrackerLog.old");
        data.rename("Arena Stats/ArenaTrackerStats.json", "Arena Stats/ArenaDudeStats.json");
    }

    //A test run (AT_SETTINGS_APP) has its own settings
    if(qEnvironmentVariableIsSet("AT_SETTINGS_APP"))    return;
    //Without fallbacks: on macOS they add the system-wide keys (AppleLocale...)
    QSettings settings;
    settings.setFallbacksEnabled(false);
    if(!settings.allKeys().isEmpty())   return;
    QSettings oldSettings("Arena Tracker", "Arena Tracker");
    oldSettings.setFallbacksEnabled(false);
    const QStringList keys = oldSettings.allKeys();
    for(const QString &key: keys)   settings.setValue(key, oldSettings.value(key));
}


QString Utility::hscardsPath()
{
    return dataPath() + "/Hearthstone Cards";
}


QString Utility::extraPath()
{
    return dataPath() + "/Extra";
}


QString Utility::histogramsPath()
{
    return dataPath() + "/Histograms";
}


QString Utility::arenaStatsPath()
{
    return dataPath() + "/Arena Stats";
}


void Utility::setCardsJson(QMap<QString, QJsonObject> *cardsJson)
{
    Utility::cardsJson = cardsJson;
}


void Utility::setLocalLang(const QString &localLang)
{
    Utility::localLang = localLang;
}


QString Utility::getLocalLang()
{
    return Utility::localLang;
}


QString Utility::removeAccents(const QString &s)
{
    if (diacriticLetters.isEmpty())
    {
        diacriticLetters = QString::fromUtf8("ŠŒŽšœžŸ¥µÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖØÙÚÛÜÝßàáâãäåæçèéêëìíîïðñòóôõöøùúûüýÿ");
        noDiacriticLetters << "S"<<"OE"<<"Z"<<"s"<<"oe"<<"z"<<"Y"<<"Y"<<"u"<<"A"<<"A"<<"A"<<"A"<<"A"<<"A"<<"AE"<<"C"<<"E"<<"E"<<"E"<<"E"<<"I"<<"I"<<"I"<<"I"<<"D"<<"N"<<"O"<<"O"<<"O"<<"O"<<"O"<<"O"<<"U"<<"U"<<"U"<<"U"<<"Y"<<"s"<<"a"<<"a"<<"a"<<"a"<<"a"<<"a"<<"ae"<<"c"<<"e"<<"e"<<"e"<<"e"<<"i"<<"i"<<"i"<<"i"<<"o"<<"n"<<"o"<<"o"<<"o"<<"o"<<"o"<<"o"<<"u"<<"u"<<"u"<<"u"<<"y"<<"y";
    }

    QString output = "";
    for (int i = 0; i < s.length(); i++)
    {
        QChar c = s[i];
        int dIndex = diacriticLetters.indexOf(c);
        if (dIndex < 0)
        {
            output.append(c);
        }
        else
        {
            QString replacement = noDiacriticLetters[dIndex];
            output.append(replacement);
        }
    }

    return output;
}


QImage Utility::getScreenshot(QScreen *screen)
{
    if(!screen)     return QImage();
    return grabScreen(screen->geometry());
}


static std::atomic<int> lastCapture{Utility::CaptureWindow};
static thread_local bool lastGrabShowsOtherWindows = false;


//The rect (points, global) of the screens, with only Hearthstone's window in it (black around it): the windows over the
//game (the tracker's, chats, notifications) aren't read. The whole screen if that capture fails; a replay's recorded
//frame in a log replay with screens.
QImage Utility::grabScreen(const QRect &rect)
{
    lastGrabShowsOtherWindows = false;
    if(ReplayScreen::isActive())    return ReplayScreen::grab(rect);

    QImage window;
    QRect frame;
    const MacScreen::Result result = MacScreen::hearthstoneWindow(window, frame);
    if(result == MacScreen::NoWindow)
    {
        lastCapture = CaptureNoWindow;
        return QImage();
    }
    if(result == MacScreen::Captured && !frame.isEmpty())
    {
        lastCapture = CaptureWindow;
        //As QScreen::grabWindow(): physical pixels, with their pixel ratio
        const qreal ratio = window.width() / static_cast<qreal>(frame.width());
        QImage canvas(qRound(rect.width()*ratio), qRound(rect.height()*ratio), QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::black);
        QPainter painter(&canvas);
        painter.drawImage(QPointF((frame.x() - rect.x())*ratio, (frame.y() - rect.y())*ratio), window);
        painter.end();
        canvas.setDevicePixelRatio(ratio);
        return canvas;
    }

    lastCapture = CaptureScreen;
    lastGrabShowsOtherWindows = true;
    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    if(!primaryScreen)  return QImage();
    return primaryScreen->grabWindow(0, rect.x(), rect.y(), rect.width(), rect.height()).toImage();
}


//The last grabScreen of this thread took the whole screen: the windows over Hearthstone are in it
bool Utility::grabShowsOtherWindows()
{
    return lastGrabShowsOtherWindows;
}


QString Utility::lastCaptureText()
{
    switch(lastCapture.load())
    {
        case CaptureWindow:     return "Hearthstone's window alone";
        case CaptureNoWindow:   return "no Hearthstone window";
        default:                return "the whole screen, the window capture failed";
    }
}


ScreenFeatures Utility::screenFeatures(QScreen *screen, QImage image)
{
    ScreenFeatures features;
    if(!screen || image.isNull())   return features;

    //Bug Fix: When using a resolution scale in you OS, draft scores will be postioned outside the screen. Now it's fixed.
    //Screen scale
    QRect rect = screen->geometry();
    features.screenScale.setX(rect.width() / static_cast<qreal>(image.width()));
    features.screenScale.setY(rect.height() / static_cast<qreal>(image.height()));
    features.screenHeight = image.height();

    cv::Mat mat(image.height(),image.width(),CV_8UC4,image.bits(), static_cast<size_t>(image.bytesPerLine()));

    //SIFT on a Retina screenshot (e.g. 3456x2160) is slow, and 20-40 times slower while macOS Game Mode throttles
    //the tracker (fullscreen Hearthstone). At about the templates' size it's 5x faster and as accurate.
    const int maxHeight = 1100;
    cv::Mat scaledMat;
    if(mat.rows > maxHeight)
    {
        features.scale = maxHeight / static_cast<double>(mat.rows);
        cv::resize(mat, scaledMat, cv::Size(), features.scale, features.scale, cv::INTER_AREA);
    }
    else    scaledMat = mat;

    features.scene = sceneFeatures(scaledMat);
    features.valid = true;
    return features;
}


std::vector<Point2f> Utility::findTemplateOnScreen(const QString &templateImage, const ScreenFeatures &screen,
                                                   const std::vector<Point2f> &templatePoints, int &goodMatches)
{
    std::vector<Point2f> screenPoints;
    goodMatches = 0;
    if(!screen.valid)   return screenPoints;

    goodMatches = findTemplateOnScene(templateImage, screen.scene, templatePoints, screenPoints, 10);
    for(Point2f &point: screenPoints)   point *= static_cast<float>(1.0/screen.scale);
    return screenPoints;
}


SceneFeatures Utility::sceneFeatures(const cv::Mat &mat)
{
    SceneFeatures scene;
    cv::cvtColor(mat, scene.gray, cv::COLOR_BGR2GRAY);
    //SURF (nonfree) is not available in current OpenCV builds, SIFT is in the main module since OpenCV 4.4.
    cv::SIFT::create()->detectAndCompute(scene.gray, cv::noArray(), scene.keypoints, scene.descriptors);
    return scene;
}


ulong Utility::findTemplateOnMat(const QString &templateImage, cv::Mat &mat)
{
    std::vector<Point2f> templatePoints, targetPoints;
    return findTemplateOnMat(templateImage, mat, templatePoints, targetPoints, 5);
}


ulong Utility::findTemplateOnMat(const QString &templateImage, cv::Mat &mat, const std::vector<Point2f> &templatePoints,
                                std::vector<Point2f> &targetPoints, ulong minGoodMatches)
{
    return findTemplateOnScene(templateImage, sceneFeatures(mat), templatePoints, targetPoints, minGoodMatches);
}


ulong Utility::findTemplateOnScene(const QString &templateImage, const SceneFeatures &scene, const std::vector<Point2f> &templatePoints,
                                  std::vector<Point2f> &targetPoints, ulong minGoodMatches)
{
    Mat img_object = imread((Utility::extraPath() + "/" + templateImage).toStdString(), cv::IMREAD_GRAYSCALE );
    if(!img_object.data)
    {
        qDebug() << "Utility: Cannot find" << templateImage;
        return 0;
    }
    const std::vector<KeyPoint> &keypoints_scene = scene.keypoints;
    const Mat &descriptors_scene = scene.descriptors;

    //-- Step 1/2: Detect keypoints and compute descriptors using SIFT (the scene's are already computed)
    cv::Ptr<cv::SIFT> sift = cv::SIFT::create();

    std::vector<KeyPoint> keypoints_object;
    Mat descriptors_object;

    //The cards in the draft templates are different from the ones on screen, and the three card frames look
    //alike, so SIFT matched template cards to neighbouring screen cards and the found rects were shifted by
    //one card. Only use the static screen frame (mask out the card area, template coordinates).
    cv::Mat objectMask;
    if(templateImage.startsWith("arenaTemplate"))
    {
        objectMask = cv::Mat(img_object.size(), CV_8UC1, cv::Scalar(255));
        objectMask(cv::Rect(150, 190, 840, 385) & cv::Rect(0, 0, img_object.cols, img_object.rows)).setTo(0);
    }

    sift->detectAndCompute( img_object, objectMask, keypoints_object, descriptors_object );
    if(keypoints_object.empty() || keypoints_scene.size() < 2)
    {
        qDebug() << "Utility: Bad screen for opencv flan.";
        return 0;
    }

    //-- Step 3: Matching descriptor vectors using FLANN matcher
    FlannBasedMatcher matcher;
    std::vector< std::vector<DMatch> > knnMatches;
    matcher.knnMatch( descriptors_object, descriptors_scene, knnMatches, 2 );

    //-- Keep only distinctive matches (Lowe's ratio test). The old SURF code used an absolute distance
    //-- threshold (0.04) that doesn't translate to SIFT descriptor distances.
    const float ratioThresh = 0.75f;
    std::vector< DMatch > good_matches;
    for(const std::vector<DMatch> &knn: knnMatches)
    {
        if(knn.size() == 2 && knn[0].distance < ratioThresh * knn[1].distance)
        {
            good_matches.push_back(knn[0]);
        }
    }
    qDebug()<< "Utility: FLANN Keypoints buenos:" <<good_matches.size();
    ulong goodMatches = good_matches.size();
    if((goodMatches < minGoodMatches) || templatePoints.empty())    return goodMatches;

    //-- Localize the object (find homography)
    std::vector<Point2f> obj;
    std::vector<Point2f> scenePoints;

    for( uint i = 0; i < good_matches.size(); i++ )
    {
      //-- Get the keypoints from the good matches
      obj.push_back( keypoints_object[ static_cast<ulong>(good_matches[i].queryIdx) ].pt );
      scenePoints.push_back( keypoints_scene[ static_cast<ulong>(good_matches[i].trainIdx) ].pt );
    }

    //Hearthstone is only ever scaled and moved on screen, never skewed. A full homography fitted to matches
    //crowded on one side of the screen (e.g. fullscreen or wide windows with big wooden margins) came out
    //in perspective, so the three card slots had different sizes. Fit only scale + translation.
    std::vector<uchar> inliers;
    Mat A = estimateAffinePartial2D( obj, scenePoints, inliers, cv::RANSAC );
    if(A.empty())   return 0;

    //No rotation either: keep the scale and take the translation as the median over the inliers
    double scale = std::hypot(A.at<double>(0,0), A.at<double>(1,0));
    std::vector<float> dx, dy;
    for(size_t i=0; i<inliers.size(); i++)
    {
        if(!inliers[i])     continue;
        dx.push_back(scenePoints[i].x - static_cast<float>(scale)*obj[i].x);
        dy.push_back(scenePoints[i].y - static_cast<float>(scale)*obj[i].y);
    }
    if(dx.empty())  return 0;
    std::nth_element(dx.begin(), dx.begin() + dx.size()/2, dx.end());
    std::nth_element(dy.begin(), dy.begin() + dy.size()/2, dy.end());
    A = (Mat_<double>(2,3) << scale, 0, dx[dx.size()/2], 0, scale, dy[dy.size()/2]);

    //-- Get the corners from the image_1 ( the object to be "detected" )
    cv::transform(templatePoints, targetPoints, A);

    return goodMatches;
}


bool Utility::isLeftOfScreen(QPoint center)
{
    int topScreen, bottomScreen, leftScreen, rightScreen;
    int midX = center.x();
    int midY = center.y();

    for(QScreen *screen: (const QList<QScreen *>)QGuiApplication::screens())
    {
        if (!screen)    continue;
        QRect screenRect = screen->geometry();
        topScreen = screenRect.y();
        bottomScreen = topScreen + screenRect.height();
        leftScreen = screenRect.x();
        rightScreen = leftScreen + screenRect.width();

        if(midX < leftScreen || midX > rightScreen ||
                midY < topScreen || midY > bottomScreen) continue;

        if(midX-leftScreen > rightScreen-midX)  return false;
        else                                    return true;
    }

    return true;
}


void Utility::dumpOnFile(const QByteArray &data, const QString &path)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly))
    {
        qDebug()<<"ERROR: Failed to create" << path;
        return;
    }

    file.write(data);
    file.close();
}


void Utility::clearLayout(QLayout* layout, bool deleteWidgets, bool recursive)
{
    while(QLayoutItem* item = layout->takeAt(0))
    {
        if(deleteWidgets)
        {
            if(QWidget* widget = item->widget())
            {
                widget->deleteLater();
            }
        }
        if(QLayout* childLayout = item->layout())
        {
            if(recursive)   clearLayout(childLayout, deleteWidgets, recursive);
        }
        if(deleteWidgets)   delete item;
    }
}


void Utility::showItemsLayout(QLayout* layout)
{
    for(int i=0; i<layout->count(); i++)
    {
        QLayoutItem *child = layout->itemAt(i);
        QWidget *widget = child->widget();
        if(widget != nullptr)  widget->show();
//        if (QLayout* childLayout = child->layout())  showItemsLayout(childLayout);
    }
}


bool Utility::createDir(const QString &pathDir)
{
    QFileInfo dirInfo(pathDir);
    if(!dirInfo.exists())
    {
        QDir().mkdir(pathDir);
        qDebug() << pathDir + " - created.";
        return true;
    }
    return false;
}


void Utility::unZip(const QString &zipName, const QString &targetPath)
{
    ZipArchive zf(zipName.toStdString());
    zf.open(ZipArchive::READ_ONLY);

    vector<ZipEntry> entries = zf.getEntries();
    vector<ZipEntry>::iterator it;
    for(it=entries.begin() ; it!=entries.end(); ++it)
    {
        ZipEntry entry = *it;
        QString name = entry.getName().data();
        int size = static_cast<int>(entry.getSize());
        if(name.endsWith('/'))
        {
            if(!name.endsWith("__MACOSX/"))
            createDir(targetPath + "/" + name);
        }
        else
        {
            char* binaryData = static_cast<char *>(entry.readAsBinary());
            QByteArray byteArray(binaryData, size);
            dumpOnFile(byteArray, targetPath + "/" + name);
            qDebug() << "Unzipped " + name;
            delete[] binaryData;
        }
    }

    zf.close();
}


//--------------------------------------------------------
//----Constants
//--------------------------------------------------------


//--------------------------------------------------------
//----Sets non collectible
//--------------------------------------------------------


bool Utility::needCodesSpecific(const QString &set)
{
    if(set == "TAVERNS_OF_TIME")    return true;
    else if(set == "TREASURES")     return true;
    else if(set == "SPACE")         return true;
    return false;
}


QStringList Utility::getSetCodesSpecific(const QString &set)
{
    if(set == "TAVERNS_OF_TIME")
    {
        return
        {
        "TOT_030",
        "TOT_056",
        "TOT_067",
        "TOT_069",
        "TOT_102",
        "TOT_103",
        "TOT_105",
        "TOT_107",
        "TOT_108",
        "TOT_109",
        "TOT_110",
        "TOT_111",
        "TOT_112",
        "TOT_116",
        "TOT_117",
        "TOT_118",
        "TOT_308",
        "TOT_313",
        "TOT_316",
        "TOT_320",
        "TOT_330",
        "TOT_332",
        "TOT_334",
        "TOT_340",
        "TOT_341",
        "TOT_342",
        "TOT_343",
        "TOT_345"
        };
    }
    //Tesoros de duelos
    else if(set == "TREASURES")
    {
        return
        {
        "PVPDR_SCH_Active44", "PVPDR_SCH_Active58", "PVPDR_SCH_Active60",
        "GILA_825", "PVPDR_SCH_Active17", "PVPDR_SCH_Active08",
        "PVPDR_SCH_Active03", "PVPDR_SCH_Active38", "PVPDR_SCH_Active50",
        "PVPDR_SCH_Active57", "PVPDR_SCH_Active30", "PVPDR_SCH_Active48",
        "PVPDR_SCH_Active31", "PVPDR_SCH_Active28", "PVPDR_SCH_Active34",
        "NAX12_04", "LOOT_998k", "LOEA_01",
        "PVPDR_YOP_Active02", "NAX11_04", "PVPDR_SCH_Active35",
        "PVPDR_SCH_Active02", "PVPDR_SCH_Active39", "PVPDR_SCH_Active42",
        "PVPDR_GUEST_Diablot7", "GILA_BOSS_35t", "NAX2_05H",
        "PVPDR_YOP_Active01", "PVPDR_SCH_Active27", "PVPDR_SCH_Active29",
        "PVPDR_SCH_Active45", "PVPDR_SCH_Active52", "PVPDR_SCH_Active61",
        "PVPDR_SCH_Active47", "PVPDR_SCH_Active46", "PVPDR_SCH_Active43"
        };
    }
    else if(set == "SPACE")
    {
        QStringList codes = Utility::getSetCodes(set, true, true);
        codes << "SC_500";
        return codes;
    }
    return {};
}

