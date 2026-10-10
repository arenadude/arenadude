#ifndef MACSCREEN_H
#define MACSCREEN_H

#include <QImage>
#include <QRect>

//Screenshots of the Hearthstone window alone (ScreenCaptureKit, macOS 14+): the windows over it (the tracker's own,
//chats, notifications) are not in them, so the OCR never reads them. Any thread.
namespace MacScreen
{
    enum Result { Captured, NoWindow, Failed };

    //The window's content in physical pixels and its frame (points, global top-left coordinates)
    Result hearthstoneWindow(QImage &image, QRect &frame);
}

#endif // MACSCREEN_H
