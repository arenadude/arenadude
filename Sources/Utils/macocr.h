#ifndef MACOCR_H
#define MACOCR_H

#include <QImage>
#include <QRect>
#include <QStringList>

//Text recognition with Apple Vision (macOS only). Used to read the names of the draft cards.
namespace MacOcr
{
    //language: Hearthstone language code (enUS, esES...). Returns the text lines found.
    QStringList recognizeLines(const QImage &image, const QString &language);

    struct TextLine
    {
        QString text;
        QRectF rect;    //In image pixels, top-left origin
    };
    //fast: Vision's fast recognizer, which also reads single characters (the accurate one drops a lone digit)
    QList<TextLine> recognizeTextLines(const QImage &image, const QString &language, bool fast=false);

    //Global geometry of the biggest on-screen Hearthstone window, or a null QRect if there is none.
    //Needs no Screen Recording permission.
    QRect hearthstoneWindowRect();
}

#endif // MACOCR_H
