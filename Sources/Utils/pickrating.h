#ifndef PICKRATING_H
#define PICKRATING_H

#include <QList>


//A Firestone winrate with K games is pulled halfway to the class mean. K comes from the class's data (empirical Bayes):
//the binomial variance over the true spread of the card winrates, K = p(1-p)/tau^2. A fixed 2000 (tau about 1.1 pt,
//the real one is about 3) made 32% over 31 games, HearthArena 1, a near average card that beat 47% over 7.9K games.
#define PICK_RATING_TRUST_GAMES     2000    //K when the data can't tell
#define PICK_RATING_TRUST_MIN       100
#define PICK_RATING_SPREAD_GAMES    1000    //Cards with this many games estimate the true spread
#define PICK_RATING_FIRE_WEIGHT     0.6f    //Real games count a bit more; HearthArena (experts) gets the rest


//One rating of a draft option from both sources: Firestone's winrate, pulled to the class mean when it has few games
//(70% over 30 games isn't better than 55% over 200K), and HearthArena's score. Each one becomes how far above the
//class's arena cards it is, in their spread, so a winrate and a score can be added.
class PickRating
{
public:
    struct Card
    {
        float fireWinrate;  //0: no data
        int fireGames;
        float haScore;      //0: no data
    };

    //The class's arena cards, the reference of both scales
    static void setPool(const QList<Card> &pool);
    static bool isReady();
    static int getTrustGames();
    //Above the class average in its spreads (0 = average); a source without data is left out
    static float rating(const Card &card);

private:
    static float fireMean, fireSpread, haMean, haSpread, trustGames;
    static bool ready;
    static float trustedWinrate(float winrate, int games);
    static float estimateTrustGames(const QList<Card> &pool);
};

#endif // PICKRATING_H
