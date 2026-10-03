#include "draftherowindow.h"
#include "mascotwindow.h"
#include "../winratesdownloader.h"
#include "scoreplate.h"
#include <QtWidgets>


#define HERO_PLATE_TOP      0.9     //Below the portrait, in portrait heights: under the class label and the name
#define HERO_CLOSE_WINRATE  1.5     //Points under the best winrate that still get a flat hand


DraftHeroWindow::DraftHeroWindow(QWidget *parent, const QList<QRect> &heroRects) :
    QMainWindow(parent, OVERLAY_WINDOW_FLAGS)
{
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_ShowWithoutActivating);
    //One window over the three plates
    QList<QRect> globalPlates;
    QRect area;
    for(const QRect &hero: heroRects)
    {
        ScorePlate::Content sizing;
        sizing.classOrder = 0;
        sizing.showFire = true;
        QRect plate(QPoint(0, 0), ScorePlate::plateSize(sizing));
        plate.moveCenter(QPoint(hero.center().x(), 0));
        plate.moveTop(static_cast<int>(hero.bottom() + HERO_PLATE_TOP*hero.height()));
        globalPlates << plate;
        area = area.united(plate);
    }
    area.adjust(-4, -4, 4, 8);      //Room for the frames' shadow
    setGeometry(area);
    for(const QRect &plate: std::as_const(globalPlates))    plateRects << plate.translated(-area.topLeft());


    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setWindowTitle("AT Heroes");
}


DraftHeroWindow::~DraftHeroWindow()
{
}


int DraftHeroWindow::handFor(float rating, float bestRating)
{
    return ScorePlate::handFor(rating, bestRating, HERO_CLOSE_WINRATE);
}


void DraftHeroWindow::setScores(int classOrder[3])
{
    for(int i=0; i<3; i++)
    {
        this->classOrder[i] = classOrder[i];
        ratings[i] = WinratesDownloader::getHeroScore(classOrder[i]);
    }
    scoresShown = true;
    update();
}


void DraftHeroWindow::hideScores(bool quick)
{
    (void)quick;
    scoresShown = false;
    update();
}


//[hand] [class icon] [Firestone: winrate, games]
void DraftHeroWindow::paintEvent(QPaintEvent *)
{
    if(!scoresShown)    return;

    QPainter painter(this);
    float bestRating = std::max(std::max(ratings[0], ratings[1]), ratings[2]);
    for(int i=0; i<3 && i<plateRects.count(); i++)
    {
        ScorePlate::Content content;
        content.hand = (ratings[i] > 0) ? handFor(ratings[i], bestRating) : -1;
        content.classOrder = classOrder[i];
        content.showFire = true;
        content.fireWinrate = ratings[i];
        content.fireGames = WinratesDownloader::getHeroGames(classOrder[i]);
        ScorePlate::paint(painter, plateRects[i], content, devicePixelRatioF());
    }
}
