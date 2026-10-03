#include "draftcard.h"
#include "../utility.h"

DraftCard::DraftCard() : DraftCard("", false)
{
}

DraftCard::DraftCard(QString code) : DraftCard(code, false)
{
}

DraftCard::DraftCard(QString code, bool gold) : DeckCard(code)
{
    this->bestQualityMatches = 1;
    this->gold = gold;
}

DraftCard::~DraftCard()
{

}


void DraftCard::setBestQualityMatch(double matchScore, bool force)
{
    if(force)   this->bestQualityMatches = matchScore;
    else        this->bestQualityMatches = std::min(matchScore,bestQualityMatches);
}


double DraftCard::getBestQualityMatches()
{
    return this->bestQualityMatches;
}


bool DraftCard::isGold()
{
    return gold;
}

