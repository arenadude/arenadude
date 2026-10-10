#include "replayscreen.h"
#include <QFileInfo>
#include <QDateTime>
#include <QGuiApplication>
#include <QMutex>
#include <QScreen>


namespace
{
    QMutex frameMutex;
    QImage frame;
    qint64 frameTime = -1, frameSize = -1;

    //The frame file, read again when the replay replaced it
    QImage currentFrame()
    {
        static const QString path = qEnvironmentVariable("AT_REPLAY_FRAME");
        QMutexLocker locker(&frameMutex);
        QFileInfo info(path);
        if(!info.exists())
        {
            frame = QImage();
            frameTime = frameSize = -1;
            return frame;
        }
        const qint64 time = info.lastModified().toMSecsSinceEpoch();
        if(time != frameTime || info.size() != frameSize)
        {
            QImage image(path);
            if(image.isNull())  return frame;   //Being replaced: the previous one
            frame = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            frameTime = time;
            frameSize = info.size();
        }
        return frame;
    }
}


bool ReplayScreen::isActive()
{
    static const bool active = qEnvironmentVariableIsSet("AT_REPLAY_FRAME");
    return active;
}


QRect ReplayScreen::hearthstoneRect()
{
    static const QRect rect = []() {
        const QStringList parts = qEnvironmentVariable("AT_REPLAY_HS_RECT").split(',');
        if(parts.count() != 4)  return QRect();
        return QRect(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
    }();
    return rect;
}


QImage ReplayScreen::grab(const QRect &rect)
{
    const QImage image = currentFrame();
    QScreen *screen = QGuiApplication::primaryScreen();
    if(image.isNull() || screen == nullptr)     return QImage();
    const qreal ratio = screen->devicePixelRatio();
    const QRect pixels(qRound(rect.x()*ratio), qRound(rect.y()*ratio), qRound(rect.width()*ratio), qRound(rect.height()*ratio));
    QImage crop = image.copy(pixels);
    crop.setDevicePixelRatio(ratio);
    return crop;
}
