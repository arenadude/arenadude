#include "splashwindow.h"
#include "mascotwindow.h"
#include <QtWidgets>


#define SPLASH_MIN_TIME     1000    //Shown at least this long, like the old splash
#define SPLASH_STALL_TIME   30000   //No download progress for this long: closed, the download goes on
#define SPLASH_WAIT_TIME    5000    //No download started and no ready signal: closed
#define SPLASH_BAR_GAP      10
#define SPLASH_BAR_HEIGHT   30
#define SPLASH_PURPLE       QColor(107, 27, 155)    //The mascot's hat, like its buttons
#define SPLASH_LINE_TIME    5000    //Each line of the bar is shown this long

//Short enough to fit the bar with the count: the player must know this only happens once
static const QStringList splashLines = {
    "Grabbing card pics",
    "One-time thing, relax",
    "Only on the first run",
    "Next time it's instant",
    "Worth the wait, trust me"
};


SplashWindow::SplashWindow(QWidget *parent)
    : QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    //The art is drawn at half its size in points (sharp on Retina)
    art = QPixmap(":/Images/Mascot/splash.png");
    artSize = art.size()/2;
    setFixedSize(artSize.width(), artSize.height() + SPLASH_BAR_GAP + SPLASH_BAR_HEIGHT);
    QScreen *screen = QGuiApplication::primaryScreen();
    if(screen != nullptr)   move(screen->availableGeometry().center() - rect().center());

    //Without any download progress (no network, or no ready signal) it doesn't stay forever
    shownClock.start();
    stallTimer.setSingleShot(true);
    connect(&stallTimer, &QTimer::timeout, this, &SplashWindow::close);
    stallTimer.start(SPLASH_WAIT_TIME);

    lineTimer.setInterval(SPLASH_LINE_TIME);
    connect(&lineTimer, &QTimer::timeout, this, [this]() {
        lineIndex = (lineIndex + 1) % splashLines.count();
        update();
    });
}


void SplashWindow::setProgress(int done, int total)
{
    this->done = done;
    this->total = total;
    stallTimer.start(SPLASH_STALL_TIME);
    if(!lineTimer.isActive())   lineTimer.start();
    update();
    if(total > 0 && done >= total)  ready();
}


//cards.json comes first: the images can't start before the tracker knows the cards
void SplashWindow::setListProgress(qint64 received, qint64 total)
{
    listReceived = received;
    listTotal = total;
    stallTimer.start(SPLASH_STALL_TIME);
    update();
}


void SplashWindow::ready()
{
    if(readySeen)   return;
    readySeen = true;
    closeWhenShownEnough();
}


void SplashWindow::closeWhenShownEnough()
{
    QTimer::singleShot(std::max<qint64>(0, SPLASH_MIN_TIME - shownClock.elapsed()), this, &SplashWindow::close);
}


void SplashWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.drawPixmap(QRect(QPoint(0, 0), artSize), art);

    const QFont font = MascotWindow::pixelFont(20);
    const QFontMetrics fm(font);
    painter.setFont(font);

    //Bubble in the empty top right corner
    const QString text = "Starting...";
    const QSize bubbleSize(fm.horizontalAdvance(text) + 24, fm.height() + 12);
    const QRect bubble(QPoint(artSize.width() - bubbleSize.width(), 0), bubbleSize);
    MascotWindow::drawPixelFrame(painter, bubble, Qt::white);
    painter.setPen(Qt::black);
    painter.drawText(bubble, Qt::AlignCenter, text);

    if(total > 0)
    {
        drawBar(painter, std::min(done, total)/static_cast<double>(total),
                QStringLiteral("%1 %2/%3").arg(splashLines[lineIndex]).arg(done).arg(total));
    }
    else if(listTotal > 0)
    {
        const double mb = 1024.0*1024.0;
        drawBar(painter, std::min(listReceived, listTotal)/static_cast<double>(listTotal),
                QStringLiteral("Grabbing the card list %1/%2 MB").arg(listReceived/mb, 0, 'f', 1).arg(listTotal/mb, 0, 'f', 1));
    }
}


//Download progress under the box: a white pixel box filled in purple
void SplashWindow::drawBar(QPainter &painter, double fraction, const QString &label)
{
    const QRect bar(0, artSize.height() + SPLASH_BAR_GAP, artSize.width(), SPLASH_BAR_HEIGHT);
    MascotWindow::drawPixelFrame(painter, bar, Qt::white);
    const int inset = 3;
    const QRect inside = bar.adjusted(inset, inset, -inset, -inset);
    QRect fill = inside;
    fill.setWidth(qRound(inside.width() * fraction));
    painter.setPen(Qt::NoPen);
    painter.setBrush(SPLASH_PURPLE);
    painter.drawRect(fill);

    //The label in white over the purple part, black over the white one
    painter.setPen(Qt::black);
    painter.drawText(inside, Qt::AlignCenter, label);
    painter.save();
    painter.setClipRect(fill);
    painter.setPen(Qt::white);
    painter.drawText(inside, Qt::AlignCenter, label);
    painter.restore();
}
