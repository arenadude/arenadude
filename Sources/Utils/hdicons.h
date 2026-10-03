#ifndef HDICONS_H
#define HDICONS_H

#include <QIcon>
#include <QString>

//Sharp class icons for Retina screens: HearthstoneJSON hero art (HDImages) in a circle. Until the art is
//downloaded an icon paints the old 32x32 image, and switches by itself on a later repaint.
namespace HDIcons
{
    QIcon hero(int classOrder);
    QIcon hero(const QString &heroLog);
    void prefetch();
}

#endif // HDICONS_H
