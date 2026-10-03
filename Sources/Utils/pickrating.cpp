#include "pickrating.h"
#include <cmath>


float PickRating::fireMean = 0, PickRating::fireSpread = 0, PickRating::haMean = 0, PickRating::haSpread = 0;
bool PickRating::ready = false;


//Mean and spread of the values
static void meanSpread(const QList<float> &values, float &mean, float &spread)
{
    mean = spread = 0;
    if(values.isEmpty())    return;
    for(float v: values)    mean += v;
    mean /= values.count();
    for(float v: values)    spread += (v - mean)*(v - mean);
    spread = std::sqrt(spread/values.count());
}


float PickRating::trustedWinrate(float winrate, int games)
{
    games = std::max(games, 0);
    return (winrate*games + fireMean*PICK_RATING_TRUST_GAMES) / (games + PICK_RATING_TRUST_GAMES);
}


void PickRating::setPool(const QList<Card> &pool)
{
    //The class mean from the winrates with enough games, then the spread of the trusted winrates
    QList<float> solid, trusted, ha;
    for(const Card &card: pool)
    {
        if(card.fireWinrate > 0 && card.fireGames >= PICK_RATING_TRUST_GAMES)    solid << card.fireWinrate;
        if(card.haScore > 0)    ha << card.haScore;
    }
    float unused;
    meanSpread(solid, fireMean, unused);
    for(const Card &card: pool)
    {
        if(card.fireWinrate > 0)    trusted << trustedWinrate(card.fireWinrate, card.fireGames);
    }
    meanSpread(trusted, unused, fireSpread);
    meanSpread(ha, haMean, haSpread);
    ready = (fireSpread > 0 || haSpread > 0);
}


bool PickRating::isReady()
{
    return ready;
}


float PickRating::rating(const Card &card)
{
    const bool fire = card.fireWinrate > 0 && fireSpread > 0;
    const bool ha = card.haScore > 0 && haSpread > 0;
    const float fireRating = fire ? (trustedWinrate(card.fireWinrate, card.fireGames) - fireMean)/fireSpread : 0;
    const float haRating = ha ? (card.haScore - haMean)/haSpread : 0;
    if(fire && ha)  return PICK_RATING_FIRE_WEIGHT*fireRating + (1 - PICK_RATING_FIRE_WEIGHT)*haRating;
    if(fire)        return fireRating;
    if(ha)          return haRating;
    return -100;    //No data: last
}
