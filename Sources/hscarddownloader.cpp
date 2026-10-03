#include "hscarddownloader.h"
#include <QtWidgets>

HSCardDownloader::HSCardDownloader(QObject *parent) : QObject(parent)
{
    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(saveWebImage(QNetworkReply*)));
}

HSCardDownloader::~HSCardDownloader()
{
    delete networkManager;
}


void HSCardDownloader::forceNextDownload()
{
    if(!pendingDownloads.isEmpty() && gettingWebCards.count() < MAX_DOWNLOADS)
    {
        downloadWebImage(pendingDownloads.takeFirst(), true);
    }
}


void HSCardDownloader::downloadWebImage(DownloadingCard downCard, bool force)
{
    downloadWebImage(downCard.code, force, downCard.fallback);
}


//The plain card comes from HearthstoneJSON's render, the golden one from the first frame of Hearthpwn's animation.
//Fallbacks: Hearthpwn's plain image for a plain card, the plain render for a golden card.
void HSCardDownloader::downloadWebImage(QString code, bool force, bool fallback)
{
    //Already downloading
    const QList<DownloadingCard> downCardList = gettingWebCards.values();
    for(const DownloadingCard &downCard: downCardList)
    {
        if(downCard.code == code)
        {
            emit pDebug("Skip download: " + code + " - Already downloading.");
            return;
        }
    }

    //Already planned to download (Eliminamos el antiguo download en pending)
    for(int i=0; i<pendingDownloads.count(); i++)
    {
        if(pendingDownloads[i].code == code)
        {
            emit pDebug("Prioritize download: " + code + " - Need for drafting.");
            pendingDownloads.removeAt(i);
            break;
        }
    }

    DownloadingCard downCard;
    downCard.code = code;
    downCard.fallback = fallback;

    if(!force && (gettingWebCards.count() >= MAX_DOWNLOADS))
    {
        pendingDownloads.prepend(downCard);
        return;
    }

    QString plainCode = code;
    bool golden = plainCode.endsWith("_premium");
    if(golden)  plainCode.chop(8);

    QString urlString;
    if(golden && !fallback)         urlString = HEARTHPWN_CARDS_GOLDEN_URL + code + "_000.png";
    else if(golden || !fallback)    urlString = HEARTHSIM_CARDS_URL + plainCode + ".png";
    else                            urlString = HEARTHPWN_CARDS_PLAIN_URL + code + ".png";

    QNetworkReply * reply = networkManager->get(QNetworkRequest(QUrl(urlString)));
    gettingWebCards[reply] = downCard;
    emit pDebug("Downloading (" + QString(urlString.startsWith(HEARTHSIM_CARDS_URL)?"HearthstoneJSON":"Hearthpwn") + "): " +
                code + " - (" + QString::number(gettingWebCards.count()) +
                ") - " + QString::number(pendingDownloads.count()));
}


void HSCardDownloader::saveWebImage(QNetworkReply * reply)
{
    reply->deleteLater();

    if(!gettingWebCards.contains(reply))    return;

    DownloadingCard downCard = gettingWebCards.take(reply);
    QString code = downCard.code;
    bool fallback = downCard.fallback;

    emit pDebug("Reply: " + code + " - (" + QString::number(gettingWebCards.count()) +
                ") - " + QString::number(pendingDownloads.count()));


    QByteArray data = reply->readAll();
    QImage webImage;
    if(reply->error() == QNetworkReply::NoError)    webImage.loadFromData(data);
    if(webImage.isNull())
    {
        if(!fallback)
        {
            emit pDebug("Failed to download card image: " + code + " - Trying the fallback.", DebugLevel::Warning);
            downloadWebImage(code, false, true);
        }
        else
        {
            emit pDebug("Failed to download card image: " + code, DebugLevel::Warning);
            emit missingOnWeb(code);
        }
    }
    else
    {
        QString plainCode = code;
        if(plainCode.endsWith("_premium"))  plainCode.chop(8);

        //HearthstoneJSON's 256x388 render, cut fitted against the old repo's HearthstoneCards images
        if(webImage.width() == 256)
        {
            webImage = webImage.copy(5, -8, 245, qRound(245*304/200.0)).scaledToWidth(200, Qt::SmoothTransformation);
        }
        //Hearthpwn now serves 600x830 images (plain and golden); the cuts below are for its old, smaller format.
        //Cut fitted against the old repo's HearthstoneCards images.
        else if(webImage.width() >= 500)
        {
            int x = -8, y = -65, w = 618;
            if(Utility::getRarityFromCode(plainCode) == LEGENDARY)
            {
                if(Utility::getTypeFromCode(plainCode) == MINION)  {x = 10; y = -20; w = 584;}
                else                                                {x = -10; y = -40; w = 596;}
            }
            webImage = webImage.copy(x, y, w, qRound(w*304/200.0)).scaledToWidth(200, Qt::SmoothTransformation);
        }
        else if(webImage.width()!=200)
        {
            CardType cardType = Utility::getTypeFromCode(plainCode);
            CardRarity cardRarity = Utility::getRarityFromCode(plainCode);
            //Golden from hearthpwn
            if(code.endsWith("_premium"))
            {
                if(cardType == MINION)
                {
                    if(cardRarity == LEGENDARY) webImage = webImage.copy(6, -8, 276, 419);
                    else                        webImage = webImage.copy(-2, -30, 290, 441);
                }
                else if(cardType == WEAPON) webImage = webImage.copy(-1, -32, 295, 447);
                else /*SPELL - HERO*/
                {
                    if(cardRarity == LEGENDARY) webImage = webImage.copy(-4, -17, 282, 429);
                    else                        webImage = webImage.copy(-4, -31, 295, 447);

                }
            }
            //Plain from hearthpwn
            else
            {
                //New cut for hearthpwn plain cards
                if(cardType == MINION)
                {
                    if(cardRarity == LEGENDARY) webImage = webImage.copy(6, -15, 283, 431);
                    else                        webImage = webImage.copy(2, -30, 290, 441);
                }
                else if(cardType == WEAPON) webImage = webImage.copy(-1, -32, 295, 447);
                else /*SPELL - HERO*/
                {
                    if(cardRarity == LEGENDARY) webImage = webImage.copy(-4, -17, 282, 429);
                    else                        webImage = webImage.copy(-4, -31, 295, 447);

                }
            }
            webImage = webImage.scaledToWidth(200, Qt::SmoothTransformation);
        }

        if(!webImage.save(Utility::hscardsPath() + "/" + code + ".png", "png"))
        {
            emit pDebug("Failed to save card image to disk: " + code, DebugLevel::Error);
            emit missingOnWeb(code);
        }
        else
        {
            emit pDebug("Card downloaded: " + code);
            emit downloaded(code);
        }
    }

    //Next download
    forceNextDownload();

    //All cards downloaded
    if(pendingDownloads.isEmpty() && gettingWebCards.isEmpty())
    {
        emit allCardsDownloaded();
    }
}


void HSCardDownloader::setLang(QString value)
{
    if(value == "esMX")         lang = "eses";
    else if(value == "enGB" ||
            value == "jaJP" ||
            value == "plPL" ||
            value == "koKR" ||
            value == "thTH")    lang = "enus";
    else                        lang = value.toLower();
}

