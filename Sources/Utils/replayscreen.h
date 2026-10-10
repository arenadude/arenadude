#ifndef REPLAYSCREEN_H
#define REPLAYSCREEN_H

#include <QImage>
#include <QRect>


//A log replay with recorded screens (tools/replay_session.py --screen): the screen captures read the current frame
//of the recording from a file, and the Hearthstone window is where it was recorded. Off in a normal run.
//  AT_REPLAY_FRAME     image file of the screen now (physical pixels), replaced by the replay; missing: nothing seen
//  AT_REPLAY_HS_RECT   "x,y,w,h" of the Hearthstone window, in points
//The screen itself (its size and pixel ratio) is the offscreen platform's, set by the replay too.
namespace ReplayScreen
{
    bool isActive();
    QRect hearthstoneRect();
    //The rect (points, global) of the current frame, like QScreen::grabWindow(0, ...).toImage(). Any thread.
    QImage grab(const QRect &rect);
}

#endif // REPLAYSCREEN_H
