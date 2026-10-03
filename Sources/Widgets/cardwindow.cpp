#include "cardwindow.h"
#include "../Utils/hdimages.h"
#include "../utility.h"
#include <QtWidgets>
    #include "../Utils/macwindow.h"

//A (non-activating) panel on macOS, like the mascot, so it also shows over fullscreen Hearthstone.
//No shadow: on macOS it drew a light line along the window edge.
CardWindow::CardWindow(QWidget *parent) :
    QMainWindow(parent, Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_ShowWithoutActivating);
    cardLabel = new QLabel(this);
    alwaysHidden = false;
    setCentralWidget(cardLabel);
    setMinimumSize(0,0);
    resize(WCARD,HCARD);
    setAttribute(Qt::WA_TranslucentBackground, true);
}


//Bounding box of the pixels that aren't (almost) transparent
QRect CardWindow::opaqueBounds(const QImage &image)
{
    QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    int top = argb.height(), bottom = -1, left = argb.width(), right = -1;
    for(int y=0; y<argb.height(); y++)
    {
        const QRgb *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for(int x=0; x<argb.width(); x++)
        {
            if(qAlpha(line[x]) <= 20)   continue;
            top = std::min(top, y);
            bottom = std::max(bottom, y);
            left = std::min(left, x);
            right = std::max(right, x);
        }
    }
    return (bottom < 0)?QRect():QRect(QPoint(left, top), QPoint(right, bottom));
}


CardWindow::~CardWindow()
{
}


void CardWindow::scale(int value_x10)
{
    if(value_x10 < 10)
    {
        alwaysHidden = true;
        hide();
    }
    else
    {
        alwaysHidden = false;
        float value = value_x10/10.0f;
        setMinimumSize(0,0);
        resize(static_cast<int>(value*WCARD), static_cast<int>(value*HCARD));
    }
}


void CardWindow::loadCard(QString code, QRect rectCard, int maxTop, int maxBottom, bool alignReverse)
{
    if(alwaysHidden || code.isEmpty() ||
        !QFileInfo::exists(Utility::hscardsPath() + "/" + code + ".png"))
    {
        hide();
        return;
    }

    QPoint center = rectCard.center();
    bool showAtLeft = !Utility::isLeftOfScreen(center);

    int winWidth = width();
    int winHeight = height();

    int moveX, moveY;
    if(alignReverse)    showAtLeft = !showAtLeft;
    if(showAtLeft)  moveX = rectCard.left()-winWidth;
    else            moveX = rectCard.right();

    moveY = center.y()-winHeight/2;
    if((maxTop!=-1) && (moveY<maxTop)) moveY=maxTop;
    else if((maxBottom!=-1) && ((moveY+winHeight)>maxBottom))
    {
        if((maxBottom-winHeight)<maxTop)    moveY=maxTop;
        else                                moveY=maxBottom-winHeight;
    }

    move(moveX, moveY);
    //HD render (512 px wide) when downloaded: same crop, scaled to the window at the screen resolution
    const qreal dpr = devicePixelRatioF();
    const QString hdFile = HDImages::path(HDImages::Render, code);
    QPixmap card = hdFile.isEmpty()?QPixmap(Utility::hscardsPath() + "/" + code + ".png"):QPixmap(hdFile);

    //Cropped to the card's opaque area: a fixed crop (made for the old images) cut the top of HD minion renders,
    //which sit higher. Fitted in the window keeping the proportions.
    const QString cacheKey = hdFile.isEmpty()?code:hdFile;
    if(!cardBounds.contains(cacheKey))  cardBounds[cacheKey] = opaqueBounds(card.toImage());
    QRect bounds = cardBounds[cacheKey];
    QPixmap cropped = bounds.isValid()?card.copy(bounds):card;
    QSize fitted = cropped.size().scaled(QSizeF(winWidth*dpr, winHeight*dpr).toSize(), Qt::KeepAspectRatio);
    QPixmap shown(QSizeF(winWidth*dpr, winHeight*dpr).toSize());
    shown.fill(Qt::transparent);
    {
        QPainter painter(&shown);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(QRect(QPoint((shown.width() - fitted.width())/2, (shown.height() - fitted.height())/2), fitted), cropped);
    }
    shown.setDevicePixelRatio(dpr);
    cardLabel->setPixmap(shown);
    show();
    MacWindow::raiseAboveFloating(this);     //Over the mascot too
}


void CardWindow::enterEvent(QEnterEvent * e)
{
    QMainWindow::enterEvent(e);
    hide();
}


void CardWindow::leaveEvent(QEvent * e)
{
    QMainWindow::leaveEvent(e);
    hide();
}
