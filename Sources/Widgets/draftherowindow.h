#ifndef DRAFTHEROWINDOW_H
#define DRAFTHEROWINDOW_H

#include <QMainWindow>
#include <QObject>
#include <QPixmap>
#include "../utility.h"


//Under each hero of the hero choice, a ScorePlate: a hand (take it / close / skip it), the class icon and the
//class winrate and games from Firestone. Each plate is centered on its hero's portrait, so it stays aligned at any screen size.
class DraftHeroWindow : public QMainWindow
{
    Q_OBJECT

//Constructor
public:
    //heroRects: the three portraits, in global coordinates
    DraftHeroWindow(QWidget *parent, const QList<QRect> &heroRects);
    ~DraftHeroWindow();

//Variables
private:
    QList<QRect> plateRects;        //In window coordinates
    int classOrder[3] = {-1, -1, -1};
    float ratings[3] = {0, 0, 0};
    bool scoresShown = false;

//Metodos
protected:
    void paintEvent(QPaintEvent *event) override;

public:
    void setScores(int classOrder[3]);
    void hideScores(bool quick=false);
    //The player's own winrates aren't shown in this design

    //Hand of a winrate against the best of the three: 0 up (the best), 1 flat (close to it), 2 down
    static int handFor(float rating, float bestRating);

signals:
    void pDebug(QString line, DebugLevel debugLevel=Normal, QString file="DraftHeroWindow");
};

#endif // DRAFTHEROWINDOW_H
