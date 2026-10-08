#ifndef DECKHANDLER_H
#define DECKHANDLER_H

#include "Cards/deckcard.h"
#include "utility.h"
#include <QObject>
#include <QJsonObject>


//The arena deck being drafted: the cards are kept sorted by cost and name, and deckCardList[0] is the
//"unknown" entry, whose total counts the cards of the 30 not known yet.
class DeckHandler : public QObject
{
    Q_OBJECT

public:
    DeckHandler(QObject *parent);
    ~DeckHandler();

//Variables
private:
    QList<DeckCard> deckCardList;
    bool inArena = false;


//Metodos
private:
    void insertDeckCard(DeckCard &deckCard);
    void newDeckCard(QString code, bool skipDups=false);
    void cardTotalMin(int index);
    void cardRemove(int index);
    void loadDraftsJson(QJsonObject &draftsJson);
    void saveDraftsJson(QJsonObject &draftsJson);

public:
    void reset();
    QList<DeckCard> *getDeckComplete();
    QList<DeckCard> getDeckCardList();
    QList<DeckCard> *getDeckCardListRef();
    int getIndexFromCode(const QString &code);
    void completeArenaDeck(QString hero);
    void redraftReviewDeck(QString bestCodesRedraftingReview[5]);
    void syncDeckSnapshot(QStringList codes);
    void refreshCardData();

signals:
    void checkCardImage(QString code);
    void pDebug(QString line, DebugLevel debugLevel=Normal, QString file="DeckHandler");

public slots:
    void newDeckCardAsset(QString code);
    void newDeckCardDraft(QString code);
    void enterArena();
    void leaveArena();
    void saveDraftDeck(QString hero);
    void deleteDraftDeck(QString hero);
};

#endif // DECKHANDLER_H
