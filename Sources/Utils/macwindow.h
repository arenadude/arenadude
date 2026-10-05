#ifndef MACWINDOW_H
#define MACWINDOW_H

#include <QObject>
#include <Qt>
#include <QPoint>
#include <QWidget>
#include <QPointer>
#include <QTimer>

//macOS window behaviour the frameless Qt windows don't get by themselves
namespace MacWindow
{
    //Real cocoa windows (false in an offscreen test run): the rest does nothing without them
    bool isNative();

    //Frameless windows can't be minimized to the Dock unless their style allows it
    void allowMiniaturize(QWidget *window);

    //One level above the stay on top windows (floating level), e.g. the mascot above the tracker's windows
    void raiseAboveFloating(QWidget *window);

    //A dialog of the tracker in front of everything: the app made active (the mascot never activates it, so the
    //dialog opened behind other apps) and the dialog above the mascot's level
    void bringToFront(QWidget *dialog);

    //Sets the cursor right away, even while another app (Hearthstone) is the active one
    void setCursorNow(Qt::CursorShape shape);

    //Screen Recording permission, without which the draft can't be seen. The request shows the system
    //prompt once; granting it takes effect after a restart of the app.
    bool hasScreenRecording();
    void requestScreenRecording();

    //A system dialog (UserNotificationCenter: e.g. macOS 15's "... requesting to bypass the system private window
    //picker", asked when the capture starts) over Hearthstone's window. The capture works, but the dialog covers the game.
    bool systemDialogOverHearthstone();
}


//A fullscreen app lives in its own Space, where other apps' windows don't appear, even the stay on top ones.
//Every stay on top window of the tracker joins all Spaces, and while Hearthstone is fullscreen the app turns
//into an accessory app (no Dock icon): macOS only shows a regular app's windows over its own fullscreen Space.
//As they join all Spaces, the draft overlays (scores, heroes, mechanics) and the mascot are only shown while Hearthstone
//is on screen.
class MacFullScreenOverlay : public QObject
{
    Q_OBJECT
public:
    explicit MacFullScreenOverlay(QObject *parent);

    static bool isHearthstoneFullScreen();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QTimer timer;
    bool accessory = false;
    bool hsOnScreen = true;

    static bool isHearthstoneOnScreen();
    static bool isFullScreenSpace();
    static bool isDraftOverlay(QWidget *widget);
    void showDraftOverlay(QWidget *widget);

private slots:
    void check();
};


//Qt only tracks the mouse on macOS while the app is active: with Hearthstone in front, hovering the
//tracker did nothing until it was clicked. While the app is inactive this polls the cursor and sends
//the enter, leave and move events Qt would send if it were active.
class MacHoverTracker : public QObject
{
    Q_OBJECT
public:
    explicit MacHoverTracker(QObject *parent);

private:
    QTimer timer;
    QPointer<QWidget> lastWidget;
    QPoint lastPos;

    void dispatchEnterLeave(QWidget *enter, QWidget *leave, const QPoint &globalPos);

private slots:
    void check();
};

#endif // MACWINDOW_H
