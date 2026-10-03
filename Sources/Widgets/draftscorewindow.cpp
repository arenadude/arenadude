#include "draftscorewindow.h"
#include "scoreplate.h"
#include "../Utils/pickrating.h"
#include <QtWidgets>


DraftScoreWindow::DraftScoreWindow(QWidget *parent, QRect rect, QSize sizeCard, int screenIndex) :
    QMainWindow(parent, OVERLAY_WINDOW_FLAGS)
{
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_ShowWithoutActivating);
#ifdef QT_DEBUG
    #if DEBUG_OVERLAYS_LEFT
        screenIndex = 0;
    #endif
    #if DEBUG_OVERLAYS_RIGHT
            screenIndex = 1;
    #endif
#endif

    const int scoreWidth = static_cast<int>(sizeCard.width()*0.7);

    QRect rectScreen;
    QList<QScreen *> screens = QGuiApplication::screens();
    if(screenIndex < screens.count())
    {
        QScreen *screen = screens[screenIndex];
        if(screen != nullptr)   rectScreen = screen->geometry();
    }

    int midCards = (rect.width() - 3*sizeCard.width())/2;
    resize(rect.width() + 2*MARGIN + midCards,
           rect.height() + 2*MARGIN - (sizeCard.height()-scoreWidth));
    move(rectScreen.x() + rect.x() - MARGIN - midCards/2,
         static_cast<int>(rectScreen.y() + rect.y() - MARGIN + 2.65*sizeCard.height()));
    for(int i=0; i<3; i++)
    {
        artRects[i] = QRect(rectScreen.x() + rect.x() + i*(rect.width() - sizeCard.width())/2, rectScreen.y() + rect.y(),
                            sizeCard.width(), sizeCard.height());
    }
    platesWindow = new ScorePlatesWindow(this, plateCenters(false));

    setCentralWidget(new QWidget(this));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setWindowTitle("AT Scores");
}


void DraftScoreWindow::setDraftMethod(bool draftMethodHA, bool draftMethodLF)
{
    showHA = draftMethodHA;
    showLF = draftMethodLF;
    if(platesShown) updatePlates();
}


void DraftScoreWindow::setScores(float rating1, float rating2, float rating3,
                                 DraftMethod draftMethod,
                                 int includedDecks1, int includedDecks2, int includedDecks3,
                                 bool restoreWindow)
{
    float ratings[3] = {rating1, rating2, rating3};
    int includedDecks[3] = {includedDecks1, includedDecks2, includedDecks3};

    for(int i=0; i<3; i++)
    {
        if(draftMethod == FireStone)
        {
            fireScores[i] = ratings[i];
            fireGames[i] = includedDecks[i];
        }
        else if(draftMethod == HearthArena)     haScores[i] = ratings[i];
    }
    platesShown = true;
    updatePlates();

    if(draftMethod == HearthArena && restoreWindow)
    {
        this->showNormal();
        this->activateWindow();
    }
}


void DraftScoreWindow::hideScores()
{
    platesWindow->clear();
    for(int i=0; i<3; i++)
    {
        fireScores[i] = haScores[i] = 0;
        fireGames[i] = -1;
    }
    platesShown = false;
}


//The hand follows the rating of both sources (PickRating)
void DraftScoreWindow::updatePlates()
{
    float ratings[3];
    for(int i=0; i<3; i++)
    {
        ratings[i] = PickRating::rating({showLF ? fireScores[i] : 0, fireGames[i], showHA ? haScores[i] : 0});
    }
    const float bestRating = std::max({ratings[0], ratings[1], ratings[2]});
    const bool anyScore = (fireScores[0] + fireScores[1] + fireScores[2] + haScores[0] + haScores[1] + haScores[2]) > 0;
    QList<ScorePlate::Content> contents;
    for(int i=0; i<3; i++)
    {
        ScorePlate::Content content;
        content.showFire = showLF;
        content.fireWinrate = fireScores[i];
        content.fireGames = fireGames[i];
        content.showHA = showHA;
        content.haScore = haScores[i];
        if(anyScore && PickRating::isReady())   content.hand = ScorePlate::handFor(ratings[i], bestRating, CLOSE_RATING);
        contents << content;
    }
    platesWindow->setContents(contents);
}


//The plates window goes with this one
void DraftScoreWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    platesWindow->show();
}


void DraftScoreWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
    platesWindow->hide();
}



//Right under each card, centered on it. The found art square sits differently on the two screens (measured on
//the 2026 client): centered on the legendary groups' cards, whose bottom is 2.82 art heights below the art top;
//0.15 art widths right of the center of the other cards, whose bottom is at 2.63.
QList<QPoint> DraftScoreWindow::plateCenters(bool legendaryGroups)
{
    const float dx = legendaryGroups ? 0 : -0.15f;
    const float bottom = legendaryGroups ? 2.82f : 2.63f;
    QList<QPoint> centers;
    for(const QRect &art: artRects)
    {
        centers << QPoint(static_cast<int>(art.center().x() + dx*art.width()),
                          static_cast<int>(art.top() + (bottom + 0.04f)*art.height()));
    }
    return centers;
}


void DraftScoreWindow::setLegendaryGroups(bool legendaryGroups)
{
    platesWindow->setTopCenters(plateCenters(legendaryGroups));
}


//The found art squares can be spaced a bit off from the real cards (seen 517 px against 545 in a 16:10 window):
//each plate goes under its card's name, and the spacing of the names corrects the scale of the height.
//Only the outer names measure the scale: a golden or legendary frame moves its banner a bit. Without both,
//scale is the last measured one (the layout doesn't change between picks). Returns the scale measured, or 0.
double DraftScoreWindow::setNameCenters(const QList<QPointF> &nameCenters, double scale)
{
    QList<int> known;
    for(int i=0; i<3 && i<nameCenters.count(); i++)     if(nameCenters[i].x() >= 0)    known << i;
    if(known.isEmpty())     return 0;

    const double artSpacing = artRects[1].center().x() - artRects[0].center().x();
    double measured = 0;
    if(known.first() == 0 && known.last() == 2 && artSpacing > 0)
    {
        measured = (nameCenters[2].x() - nameCenters[0].x()) / (2 * artSpacing);
        //A wrong name line, not a different layout
        if(measured < 0.85 || measured > 1.2)
        {
            emit pDebug("Plates: names spacing doesn't fit the cards (scale " + QString::number(measured, 'f', 3) + "), kept.");
            return 0;
        }
        scale = measured;
    }
    if(scale <= 0)  scale = 1;

    QList<QPoint> centers = plateCenters(false);
    const QRect &art = artRects[0];
    const int top = static_cast<int>(art.top() + (2.63f + 0.04f) * art.height() * scale);
    for(int i=0; i<3; i++)
    {
        //A name not read: from the nearest read one
        int ref = known.first();
        for(int k: known)   if(std::abs(k - i) < std::abs(ref - i))     ref = k;
        const double x = nameCenters[ref].x() + (i - ref) * artSpacing * scale;
        centers[i] = QPoint(static_cast<int>(x), top);
    }
    emit pDebug("Plates by names: scale " + QString::number(scale, 'f', 3) + (measured > 0 ? "" : " (last)") + ", x " +
                QString::number(centers[0].x()) + "/" + QString::number(centers[1].x()) + "/" + QString::number(centers[2].x()));
    platesWindow->setTopCenters(centers);
    return measured;
}
