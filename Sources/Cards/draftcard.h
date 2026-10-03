#ifndef DRAFTCARD_H
#define DRAFTCARD_H

#include "deckcard.h"


class DraftCard : public DeckCard
{
public:
    DraftCard();
    DraftCard(QString code);
    DraftCard(QString code, bool gold);
    ~DraftCard();

//Variables
private:
    double bestQualityMatches;
    bool gold;

//Metodos
public:
    double getBestQualityMatches();
    void setBestQualityMatch(double matchScore, bool force);
    bool isGold();
};

#endif // DRAFTCARD_H
