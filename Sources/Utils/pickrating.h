#ifndef PICKRATING_H
#define PICKRATING_H

#include <QList>


#define PICK_RATING_TRUST_GAMES     2000    //A Firestone winrate with this many games is pulled halfway to the class mean
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
    //Above the class average in its spreads (0 = average); a source without data is left out
    static float rating(const Card &card);

private:
    static float fireMean, fireSpread, haMean, haSpread;
    static bool ready;
    static float trustedWinrate(float winrate, int games);
};

#endif // PICKRATING_H
