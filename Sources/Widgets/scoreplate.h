#ifndef SCOREPLATE_H
#define SCOREPLATE_H

#include <QWidget>
#include <QMainWindow>
#include <QPixmap>


//A pick's scores in the mascot's pixel style, under a hero or a draft card:
//[hand] [class icon] [Firestone flame: winrate, games] [HearthArena crown: score]
class ScorePlate : public QWidget
{
    Q_OBJECT
public:
    struct Content
    {
        int hand = -1;              //0 up (take it), 1 flat (close to the best), 2 down; -1 none
        int classOrder = -1;        //Class icon for heroes; -1 none
        bool showFire = false;
        float fireWinrate = 0;      //0: no data
        int fireGames = -1;         //-1: unknown
        bool showHA = false;
        float haScore = 0;
    };

    explicit ScorePlate(QWidget *parent = nullptr);

    void setContent(const Content &content);
    void clear();

    static QSize plateSize(const Content &content);
    static void paint(QPainter &painter, const QRect &plate, const Content &content, qreal dpr);
    //Hand of a score against the best of the three: up for the best, flat within closeMargin of it, down below
    static int handFor(float score, float bestScore, float closeMargin);
    static QString gamesText(int games);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Content content;
    bool shown = false;
};


//Plates in their own overlay window, each centered on a pick: its top center is given in global coordinates
class ScorePlatesWindow : public QMainWindow
{
    Q_OBJECT
public:
    ScorePlatesWindow(QWidget *parent, const QList<QPoint> &topCenters);

    void setContents(const QList<ScorePlate::Content> &contents);
    void clear();
    void setTopCenters(const QList<QPoint> &topCenters);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QList<QPoint> topCenters;       //In window coordinates
    QList<ScorePlate::Content> contents;
};

#endif // SCOREPLATE_H
