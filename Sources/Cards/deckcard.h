#ifndef DECKCARD_H
#define DECKCARD_H

#include <QString>
#include <QList>
#include "../constants.h"
#include "../utility.h"


//A card of the deck (or a draft candidate): its cards.json data, the copies and the scores of the redraft list
class DeckCard
{
public:
    DeckCard(QString code);
    ~DeckCard();

//Variables
public:
    int total;
    int remaining;

protected:
    QString code, name;
    CardRarity rarity;
    CardType type;
    int cost;

private:
    int scoreHA;
    float scoreFire;
    int samplesFire;
    int classOrder;

//Metodos
public:
    bool isCode(const QString &code);
    QString getCode() const;
    CardType getType();
    QString getName() const;
    CardRarity getRarity();
    int getCost() const;
    void setCode(QString code);
    float getScore(DraftMethod draftMethod) const;
    void setScores(int haTier, float fireWR, int classOrder, int samplesFire);
};

#endif // DECKCARD_H
