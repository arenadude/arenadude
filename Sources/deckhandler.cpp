#include "deckhandler.h"
#include <QtWidgets>

DeckHandler::DeckHandler(QObject *parent) : QObject(parent)
{
    reset();
}

DeckHandler::~DeckHandler()
{
    deckCardList.clear();
}


void DeckHandler::reset()
{
    deckCardList.clear();

    DeckCard deckCard("");
    deckCard.total = 30;
    deckCard.remaining = -1;
    deckCardList << deckCard;

    emit pDebug("Deck list cleared.");
}


QList<DeckCard> * DeckHandler::getDeckComplete()
{
    if(deckCardList[0].total==0)    return &deckCardList;
    else    return nullptr;
}


QList<DeckCard> DeckHandler::getDeckCardList()
{
    return deckCardList;
}


QList<DeckCard> * DeckHandler::getDeckCardListRef()
{
    return &deckCardList;
}


int DeckHandler::getIndexFromCode(const QString &code)
{
    if(code.isEmpty())  return -1;

    for(int i=0; i<deckCardList.size(); i++)
    {
        if(deckCardList[i].isCode(code))    return i;
    }
    return -1;
}


void DeckHandler::newDeckCardAsset(QString code)
{
    newDeckCard(code, true);
}


void DeckHandler::newDeckCardDraft(QString code)
{
    newDeckCard(code);
}


//skipDups: a card already in the deck isn't added again
void DeckHandler::newDeckCard(QString code, bool skipDups)
{
    if(code.isEmpty())  return;

    int index = getIndexFromCode(code);
    if(index > 0)
    {
        if(skipDups)    return;
        deckCardList[index].total++;
        deckCardList[index].remaining++;
    }
    else
    {
        DeckCard deckCard(code);
        deckCard.total = 1;
        deckCard.remaining = 1;
        insertDeckCard(deckCard);
        emit checkCardImage(code);
    }
    deckCardList[0].total--;

    emit pDebug("Add to deck: " + Utility::getCardAttribute(code, "name").toString());
}


//By cost, then by name, after the unknown entry
void DeckHandler::insertDeckCard(DeckCard &deckCard)
{
    for(int i=1; i<deckCardList.length(); i++)
    {
        if(deckCard.getCost() < deckCardList[i].getCost() ||
            (deckCard.getCost() == deckCardList[i].getCost() && deckCard.getName().toLower() < deckCardList[i].getName().toLower()))
        {
            deckCardList.insert(i, deckCard);
            return;
        }
    }
    deckCardList.append(deckCard);
}


void DeckHandler::cardTotalMin(int index)
{
    deckCardList[index].total--;
    deckCardList[index].remaining = deckCardList[index].total;
    deckCardList[0].total++;
}


void DeckHandler::cardRemove(int index)
{
    deckCardList.removeAt(index);
    deckCardList[0].total++;
}


void DeckHandler::enterArena()
{
    emit pDebug("Enter arena");
    this->inArena = true;
}


void DeckHandler::leaveArena()
{
    if(!inArena)    return;

    emit pDebug("Leave arena");
    this->inArena = false;
    reset();
}


void DeckHandler::loadDraftsJson(QJsonObject &draftsJson)
{
    QFile jsonFile(Utility::dataPath() + "/ArenaDudeDrafts.json");
    if(jsonFile.exists())
    {
        if(!jsonFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            emit pDebug("Failed to load ArenaDudeDrafts.json from disk.", DebugLevel::Error);
            return;
        }
        QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonFile.readAll());
        jsonFile.close();

        draftsJson = jsonDoc.object();
    }
}


void DeckHandler::saveDraftsJson(QJsonObject &draftsJson)
{
    QJsonDocument jsonDoc;
    jsonDoc.setObject(draftsJson);

    QFile jsonFile(Utility::dataPath() + "/ArenaDudeDrafts.json");
    if(!jsonFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        emit pDebug("Failed to create ArenaDudeDrafts.json on disk.", DebugLevel::Error);
        return;
    }
    jsonFile.write(jsonDoc.toJson());
    jsonFile.close();
}


void DeckHandler::redraftReviewDeck(QString bestCodesRedraftingReview[5])
{
    for(int i=0; i<5; i++)
    {
        int index = getIndexFromCode(bestCodesRedraftingReview[i]);
        if(index == -1)
        {
            emit pDebug("Redraft review deck: Code " + bestCodesRedraftingReview[i] + " NOT FOUND.", DebugLevel::Warning);
        }
        else
        {
            if(deckCardList[index].total > 1)
            {
                emit pDebug("Redraft review deck: Code " + bestCodesRedraftingReview[i] + " sub 1. Remaining: " + QString::number(deckCardList[index].total-1));
                cardTotalMin(index);
            }
            else
            {
                emit pDebug("Redraft review deck: Code " + bestCodesRedraftingReview[i] + " removed.");
                cardRemove(index);
            }
        }
    }
}


//Hearthstone's list of the deck (Arena.log snapshot) corrects the tracker's: cards it doesn't list were discarded
//(the redraft review reads the discarded ones by OCR, which can be wrong), the ones it lists are in the deck.
//The snapshot lists a card once even with 2 copies: copies are kept as they are.
void DeckHandler::syncDeckSnapshot(QStringList codes)
{
    const QSet<QString> present(codes.begin(), codes.end());
    QStringList removed, added;
    for(int i=deckCardList.count()-1; i>=1; i--)
    {
        const QString code = deckCardList[i].getCode();
        if(code.isEmpty() || present.contains(code))    continue;
        removed << Utility::cardEnNameFromCode(code);
        while(deckCardList[i].total > 1)    cardTotalMin(i);
        cardRemove(i);
    }
    for(const QString &code: present)
    {
        if(getIndexFromCode(code) != -1)    continue;
        added << Utility::cardEnNameFromCode(code);
        newDeckCard(code);
    }
    if(!removed.isEmpty() || !added.isEmpty())
        emit pDebug("Deck snapshot sync: removed " + removed.join(", ") + " - added " + added.join(", "));
}


void DeckHandler::saveDraftDeck(QString hero)
{
    if(Utility::classLogNumber2classEnum(hero) == INVALID_CLASS || deckCardList[0].total != 0)
    {
        emit pDebug("Save draft deck FAIL/Delete draft deck: Hero " + hero + " - " +
                    QString::number(deckCardList[0].total) + " unknown cards.", DebugLevel::Warning);
        deleteDraftDeck(hero);
        return;
    }

    emit pDebug("Save draft deck: Hero " + hero);

    QJsonObject draftsJson;
    loadDraftsJson(draftsJson);

    QJsonObject draftObject;
    for(DeckCard &deckCard: deckCardList)
    {
        QString code = deckCard.getCode();
        if(!code.isEmpty()) draftObject[code] = deckCard.total;
    }

    draftsJson[hero] = draftObject;
    saveDraftsJson(draftsJson);
}


void DeckHandler::deleteDraftDeck(QString hero)
{
    emit pDebug("Delete draft deck: Hero " + hero);
    if(Utility::classLogNumber2classEnum(hero) == INVALID_CLASS)    return;

    QJsonObject draftsJson;
    loadDraftsJson(draftsJson);

    QJsonObject draftObject;
    draftsJson[hero] = draftObject;
    saveDraftsJson(draftsJson);
}


//Hearthstone's list of the deck (Arena.log) says which cards are in it; it may list a card once with 2 copies, so the
//saved draft only gives the copies of those cards. It used to replace the deck when they were close: a draft saved
//with wrong redraft discards (read by OCR) then brought the discarded cards back.
void DeckHandler::completeArenaDeck(QString hero)
{
    QMap<QString, int> logTotals;
    for(int i=1; i<deckCardList.count(); i++)
    {
        const QString code = deckCardList[i].getCode();
        if(!code.isEmpty())     logTotals[code] += deckCardList[i].total;
    }

    QJsonObject draftsJson;
    loadDraftsJson(draftsJson);

    if(!draftsJson.contains(hero))
    {
        emit pDebug("Completing Arena Deck: Hero " + hero + " draft not found.");
        return;
    }
    QJsonObject draftObject = draftsJson[hero].toObject();

    //Nothing listed: the saved draft is all there is
    if(logTotals.isEmpty())
    {
        emit pDebug("Completing Arena Deck: Hero " + hero + " - Empty decklist, load the draft deck.");
        reset();
        for(const QString &code: draftObject.keys())
        {
            for(int i=0; i<draftObject[code].toInt(); i++)  newDeckCardDraft(code);
        }
        return;
    }

    int extraCopies = 0;
    reset();
    for(auto it=logTotals.constBegin(); it!=logTotals.constEnd(); it++)
    {
        const int copies = std::max(it.value(), draftObject.value(it.key()).toInt());
        extraCopies += copies - it.value();
        for(int i=0; i<copies; i++)     newDeckCardDraft(it.key());
    }
    emit pDebug("Completing Arena Deck: Hero " + hero + " - " + QString::number(logTotals.count()) +
                " cards from the decklist, " + QString::number(extraCopies) + " extra copies from the draft.");
}
