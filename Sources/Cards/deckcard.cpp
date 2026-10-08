#include "deckcard.h"


DeckCard::DeckCard(QString code)
{
    setCode(code);
    total = remaining = 1;
    scoreHA = 0;
    scoreFire = 0;
    samplesFire = 0;
    classOrder = -1;
}


DeckCard::~DeckCard()
{

}


float DeckCard::getScore(DraftMethod draftMethod) const
{
    switch(draftMethod)
    {
        case HearthArena:   return scoreHA;
        case FireStone:     return scoreFire;
        default:            return 0;
    }
}


void DeckCard::setScores(int haTier, float fireWR, int classOrder, int samplesFire)
{
    this->scoreHA = haTier;
    this->scoreFire = fireWR;
    this->samplesFire = samplesFire;
    this->classOrder = classOrder;
}


void DeckCard::setCode(QString code)
{
    this->code = code;

    if(!code.isEmpty())
    {
        cost = Utility::getCardAttribute(code, "cost").toInt();
        type = Utility::getTypeFromCode(code);
        name = Utility::getCardAttribute(code, "name").toString();
        rarity = Utility::getRarityFromCode(code);
    }
    else
    {
        cost = -1;
        type = INVALID_TYPE;
        name = "unknown";
        rarity = INVALID_RARITY;
    }
}


bool DeckCard::isCode(const QString &code)
{
    return (code == this->code);
}


QString DeckCard::getCode() const
{
    return code;
}


CardType DeckCard::getType()
{
    return type;
}


QString DeckCard::getName() const
{
    return name;
}


int DeckCard::getCost() const
{
    return cost;
}


CardRarity DeckCard::getRarity()
{
    return rarity;
}

