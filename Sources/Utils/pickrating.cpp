#include "pickrating.h"
#include <cmath>
#include <algorithm>


float PickRating::fireMean = 0, PickRating::fireSpread = 0, PickRating::haMean = 0, PickRating::haSpread = 0;
float PickRating::trustGames = PICK_RATING_TRUST_GAMES;
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
    return (winrate*games + fireMean*trustGames) / (games + trustGames);
}


//The spread of the observed winrates minus their sampling noise is the true spread of the cards
float PickRating::estimateTrustGames(const QList<Card> &pool)
{
    QList<float> rates;
    float noise = 0;
    for(const Card &card: pool)
    {
        if(card.fireWinrate <= 0 || card.fireGames < PICK_RATING_SPREAD_GAMES)  continue;
        const float p = card.fireWinrate/100;
        rates << p;
        noise += p*(1 - p)/card.fireGames;
    }
    if(rates.count() < 10)  return PICK_RATING_TRUST_GAMES;
    float mean, spread;
    meanSpread(rates, mean, spread);
    const float trueVariance = spread*spread - noise/rates.count();
    if(trueVariance <= 0)   return PICK_RATING_TRUST_GAMES;
    return std::clamp(mean*(1 - mean)/trueVariance, float(PICK_RATING_TRUST_MIN), float(PICK_RATING_TRUST_GAMES));
}


void PickRating::setPool(const QList<Card> &pool)
{
    //How much a winrate is trusted, the class mean from the winrates with enough games, then the spread of the
    //trusted winrates
    trustGames = estimateTrustGames(pool);
    QList<float> solid, trusted, ha;
    for(const Card &card: pool)
    {
        if(card.fireWinrate > 0 && card.fireGames >= trustGames)    solid << card.fireWinrate;
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


int PickRating::getTrustGames()
{
    return qRound(trustGames);
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
