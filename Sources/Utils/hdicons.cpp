#include "hdicons.h"
#include "hdimages.h"
#include "../utility.h"
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QMap>
#include <functional>

#define HD_ICON_SIZE 128


//Paints the HD pixmap when available, else the theme file
class HDIconEngine : public QIconEngine
{
public:
    HDIconEngine(std::function<QPixmap()> hdPixmap, const QString &fallbackFile) :
        hdPixmap(hdPixmap), fallbackFile(fallbackFile) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        Q_UNUSED(mode); Q_UNUSED(state);
        QPixmap pixmap = hdPixmap();
        if(pixmap.isNull())     pixmap = QPixmap(fallbackFile);
        if(pixmap.isNull())     return;
        painter->save();
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->drawPixmap(rect, pixmap);
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap canvas(QSizeF(size.width()*scale, size.height()*scale).toSize());
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        paint(&painter, QRect(QPoint(0,0), canvas.size()), mode, state);
        painter.end();
        canvas.setDevicePixelRatio(scale);
        return canvas;
    }

    QIconEngine *clone() const override
    {
        return new HDIconEngine(hdPixmap, fallbackFile);
    }

private:
    std::function<QPixmap()> hdPixmap;
    QString fallbackFile;
};


//Art in a circle with a gold ring; null until the art is downloaded
static QPixmap circleArt(const QString &code, qreal zoom)
{
    static QMap<QString, QPixmap> cache;
    if(cache.contains(code))    return cache[code];

    const QString file = HDImages::path(HDImages::Portrait, code);
    if(file.isEmpty())  return QPixmap();
    QPixmap art(file);
    if(art.isNull())    return QPixmap();

    const qreal s = HD_ICON_SIZE;
    QPixmap canvas(HD_ICON_SIZE, HD_ICON_SIZE);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const qreal ring = s*0.06;
    QRectF inner(ring, ring, s - 2*ring, s - 2*ring);
    QPainterPath clip;
    clip.addEllipse(inner);
    painter.setClipPath(clip);
    //Center of the art, zoomed in
    qreal side = qMin(art.width(), art.height())/zoom;
    painter.drawPixmap(inner, art, QRectF((art.width()-side)/2, (art.height()-side)/2, side, side));
    painter.setClipping(false);

    QLinearGradient gold(0, 0, 0, s);
    gold.setColorAt(0, QColor(250, 222, 140));
    gold.setColorAt(0.5, QColor(196, 150, 60));
    gold.setColorAt(1, QColor(120, 82, 28));
    painter.setPen(QPen(QBrush(gold), ring));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(ring/2, ring/2, s - ring, s - ring));
    painter.end();

    cache[code] = canvas;
    return canvas;
}


QIcon HDIcons::hero(int classOrder)
{
    const QString code = QStringLiteral("HERO_%1").arg(Utility::classOrder2classLogNumber(classOrder));
    return QIcon(new HDIconEngine([code]() { return circleArt(code, 1.25); }, ":/Images/hero" + Utility::classOrder2classLogNumber(classOrder) + ".png"));
}


QIcon HDIcons::hero(const QString &heroLog)
{
    return hero(Utility::classLogNumber2classOrder(heroLog));
}


//Asks for the art early, so it is there when the icons are painted
void HDIcons::prefetch()
{
    for(int i=0; i<NUM_HEROS; i++)
    {
        HDImages::path(HDImages::Portrait, QStringLiteral("HERO_%1").arg(Utility::classOrder2classLogNumber(i)));
    }
}
