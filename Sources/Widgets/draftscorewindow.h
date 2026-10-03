#ifndef DRAFTSCOREWINDOW_H
#define DRAFTSCOREWINDOW_H

#include <QMainWindow>
#include <QObject>
#include "../utility.h"


#define MARGIN 10
#define CLOSE_RATING        0.25f   //Pick rating (class spreads) under the best card that still gets a flat hand


class ScorePlatesWindow;

//The overlay of the draft cards: a plate under each card (ScorePlatesWindow) with its scores and the hand
class DraftScoreWindow : public QMainWindow
{
    Q_OBJECT

//Constructor
public:
    DraftScoreWindow(QWidget *parent, QRect rect, QSize sizeCard, int screenIndex);

//Variables
private:
    ScorePlatesWindow *platesWindow;
    QRect artRects[3];                  //Global, where the draft found each card's art
    float fireScores[3] = {0, 0, 0}, haScores[3] = {0, 0, 0};
    int fireGames[3] = {-1, -1, -1};
    bool platesShown = false;
    bool showHA = false, showLF = false;


//Metodos
private:
    void updatePlates();
    QList<QPoint> plateCenters(bool legendaryGroups);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

public:
    void setScores(float rating1, float rating2, float rating3, DraftMethod draftMethod,
                   int includedDecks1, int includedDecks2, int includedDecks3, bool restoreWindow);
    void hideScores();
    void setLegendaryGroups(bool legendaryGroups);     //The first pick: the plates sit differently
    //Global, (-1,-1) for a name not read
    double setNameCenters(const QList<QPointF> &nameCenters, double scale);
    void setDraftMethod(bool draftMethodHA, bool draftMethodLF);

signals:
    void pDebug(QString line, DebugLevel debugLevel=Normal, QString file="DraftScoreWindow");
};

#endif // DRAFTSCOREWINDOW_H
