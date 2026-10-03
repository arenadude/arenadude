#include "scoreplate.h"
#include "mascotwindow.h"
#include "../Utils/hdicons.h"
#include "../utility.h"
#include <QtWidgets>


#define PLATE_HEIGHT        66
#define PLATE_PADDING       14      //Inside the frame, left and right
#define PLATE_GAP           10      //Between the columns
#define PLATE_HAND_SIZE     46
#define PLATE_ICON_SIZE     46      //As big as the hand
#define PLATE_FIRE_WIDTH    66
#define PLATE_HA_WIDTH      40
#define PLATE_LOGO_HEIGHT   18      //Firestone's flame, HearthArena's crown
#define PLATE_SCORE_SIZE    22
#define PLATE_GAMES_SIZE    14


//The hands and the source logos, loaded once
static const QPixmap &handPixmap(int hand)
{
    static QPixmap hands[3] = {QPixmap(":/Images/Mascot/thumb_up.png"), QPixmap(":/Images/Mascot/hand_flat.png"),
                               QPixmap(":/Images/Mascot/thumb_down.png")};
    return hands[hand];
}


static const QPixmap &fireLogo()
{
    static QPixmap logo = QPixmap(":/Images/lfText.png").copy(QRect(45, 0, 37, 49));        //The flame of the old badge
    return logo;
}


static const QPixmap &haLogo()
{
    static QPixmap logo = QPixmap(":/Images/haText.png").copy(QRect(32, 0, 65, 39));        //The crown of the old badge
    return logo;
}


ScorePlate::ScorePlate(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(plateSize(content) + QSize(0, 4));     //Room for the shadow
}


void ScorePlate::setContent(const Content &content)
{
    this->content = content;
    shown = true;
    setFixedSize(plateSize(content) + QSize(0, 4));
    update();
}


void ScorePlate::clear()
{
    shown = false;
    update();
}


QSize ScorePlate::plateSize(const Content &content)
{
    int width = 2*PLATE_PADDING + PLATE_HAND_SIZE;
    if(content.classOrder >= 0) width += PLATE_GAP + PLATE_ICON_SIZE;
    if(content.showFire)        width += PLATE_GAP + PLATE_FIRE_WIDTH;
    if(content.showHA)          width += PLATE_GAP + PLATE_HA_WIDTH;
    return QSize(width, PLATE_HEIGHT);
}


int ScorePlate::handFor(float score, float bestScore, float closeMargin)
{
    if(FLOATEQ(score, bestScore))           return 0;
    if(bestScore - score <= closeMargin)    return 1;
    return 2;
}


QString ScorePlate::gamesText(int games)
{
    if(games < 0)       return QString();
    if(games < 1000)    return QString::number(games) + " games";
    if(games < 10000)   return QString::number(games/1000.0, 'f', 1) + "K games";
    return QString::number(qRound(games/1000.0)) + "K games";
}


void ScorePlate::paint(QPainter &painter, const QRect &plate, const Content &content, qreal dpr)
{
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);
    MascotWindow::drawPixelFrame(painter, plate.translated(0, 3), Qt::black);      //Shadow
    MascotWindow::drawPixelFrame(painter, plate, Qt::white);

    int x = plate.x() + PLATE_PADDING;
    const int midY = plate.center().y();
    if(content.hand >= 0 && content.hand < 3)
    {
        const QPixmap &hand = handPixmap(content.hand);
        QSize size = hand.size().scaled(PLATE_HAND_SIZE, PLATE_HAND_SIZE, Qt::KeepAspectRatio);
        painter.drawPixmap(QRect(QPoint(x + (PLATE_HAND_SIZE - size.width())/2, midY - size.height()/2), size), hand);
    }
    x += PLATE_HAND_SIZE;

    if(content.classOrder >= 0)
    {
        x += PLATE_GAP;
        QPixmap icon = HDIcons::hero(content.classOrder).pixmap(QSize(PLATE_ICON_SIZE, PLATE_ICON_SIZE), dpr);
        painter.drawPixmap(QRect(x, midY - PLATE_ICON_SIZE/2, PLATE_ICON_SIZE, PLATE_ICON_SIZE), icon);
        x += PLATE_ICON_SIZE;
    }

    //A source column: its logo on top, the score under it, and an optional smaller line (games)
    auto drawColumn = [&](int width, const QPixmap &logo, const QString &score, const QString &small) {
        x += PLATE_GAP;
        QRect column(x, plate.y() + 5, width, plate.height() - 10);
        int contentH = PLATE_LOGO_HEIGHT + PLATE_SCORE_SIZE + (small.isEmpty() ? 0 : PLATE_GAMES_SIZE);
        int y = column.y() + (column.height() - contentH)/2;
        QSize logoSize = logo.size().scaled(width, PLATE_LOGO_HEIGHT, Qt::KeepAspectRatio);
        painter.drawPixmap(QRect(QPoint(column.center().x() - logoSize.width()/2, y), logoSize), logo);
        y += PLATE_LOGO_HEIGHT;
        painter.setPen(Qt::black);
        painter.setFont(MascotWindow::pixelFont(PLATE_SCORE_SIZE));
        painter.drawText(QRect(column.x() - 4, y, column.width() + 8, PLATE_SCORE_SIZE), Qt::AlignCenter, score);
        y += PLATE_SCORE_SIZE;
        if(!small.isEmpty())
        {
            painter.setPen(QColor(100, 100, 100));
            painter.setFont(MascotWindow::pixelFont(PLATE_GAMES_SIZE));
            painter.drawText(QRect(column.x() - 4, y, column.width() + 8, PLATE_GAMES_SIZE), Qt::AlignCenter, small);
        }
        x += width;
    };

    if(content.showFire)
    {
        QString score = (content.fireWinrate > 0) ? QString::number(content.fireWinrate, 'f', 1) + "%" : "?";
        drawColumn(PLATE_FIRE_WIDTH, fireLogo(), score, (content.fireWinrate > 0) ? gamesText(content.fireGames) : QString());
    }
    if(content.showHA)
    {
        QString score = (content.haScore > 0) ? QString::number(qRound(content.haScore)) : "?";
        drawColumn(PLATE_HA_WIDTH, haLogo(), score, QString());
    }
}


void ScorePlate::paintEvent(QPaintEvent *)
{
    if(!shown)  return;
    QPainter painter(this);
    paint(painter, QRect(0, 0, width(), PLATE_HEIGHT), content, devicePixelRatioF());
}



ScorePlatesWindow::ScorePlatesWindow(QWidget *parent, const QList<QPoint> &topCenters) :
    QMainWindow(parent, OVERLAY_WINDOW_FLAGS)
{
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setWindowTitle("AT Plates");

    setTopCenters(topCenters);
}


//Big enough for the widest plate under each pick
void ScorePlatesWindow::setTopCenters(const QList<QPoint> &topCenters)
{
    ScorePlate::Content widest;
    widest.classOrder = 0;
    widest.showFire = widest.showHA = true;
    const QSize size = ScorePlate::plateSize(widest);
    QRect area;
    for(const QPoint &center: topCenters)   area = area.united(QRect(center.x() - size.width()/2, center.y(), size.width(), size.height()));
    area.adjust(-4, -4, 4, 8);      //Room for the frames' shadow
    setGeometry(area);
    this->topCenters.clear();
    for(const QPoint &center: topCenters)   this->topCenters << center - area.topLeft();
    update();
}


void ScorePlatesWindow::setContents(const QList<ScorePlate::Content> &contents)
{
    this->contents = contents;
    update();
}


void ScorePlatesWindow::clear()
{
    contents.clear();
    update();
}


void ScorePlatesWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    for(int i=0; i<contents.count() && i<topCenters.count(); i++)
    {
        QSize size = ScorePlate::plateSize(contents[i]);
        ScorePlate::paint(painter, QRect(QPoint(topCenters[i].x() - size.width()/2, topCenters[i].y()), size),
                          contents[i], devicePixelRatioF());
    }
}
