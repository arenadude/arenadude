#include "macwindow.h"
#include <QtWidgets>
#import <AppKit/AppKit.h>


bool MacWindow::isNative()
{
    return QGuiApplication::platformName() == "cocoa";
}


//nil without a cocoa window: a test run on the offscreen platform has no NSView behind winId()
static NSView *nativeView(QWidget *widget)
{
    if(widget == nullptr || !MacWindow::isNative())     return nil;
    return (__bridge NSView *)reinterpret_cast<void *>(widget->winId());
}


void MacWindow::allowMiniaturize(QWidget *window)
{
    if(window == nullptr)   return;
    NSView *view = nativeView(window);
    if(view == nil || view.window == nil)   return;
    view.window.styleMask |= NSWindowStyleMaskMiniaturizable;
}


//Private CoreGraphics connection property (used by browsers): lets a background app set the cursor over its windows
extern "C" int CGSMainConnectionID(void);
extern "C" CGError CGSSetConnectionProperty(int connection, int targetConnection, CFStringRef key, CFTypeRef value);


bool MacWindow::hasScreenRecording()
{
    return CGPreflightScreenCaptureAccess();
}


void MacWindow::requestScreenRecording()
{
    CGRequestScreenCaptureAccess();
}


void MacWindow::setCursorNow(Qt::CursorShape shape)
{
    static bool backgroundCursor = false;
    if(!backgroundCursor)
    {
        int connection = CGSMainConnectionID();
        CGSSetConnectionProperty(connection, connection, CFSTR("SetsCursorInBackground"), kCFBooleanTrue);
        backgroundCursor = true;
    }

    NSCursor *cursor;
    switch(shape)
    {
        case Qt::PointingHandCursor:    cursor = [NSCursor pointingHandCursor];     break;
        case Qt::OpenHandCursor:        cursor = [NSCursor openHandCursor];         break;
        case Qt::ClosedHandCursor:      cursor = [NSCursor closedHandCursor];       break;
        default:                        cursor = [NSCursor arrowCursor];            break;
    }
    [cursor set];
}


void MacWindow::raiseAboveFloating(QWidget *window)
{
    if(window == nullptr)   return;
    NSView *view = nativeView(window);
    if(view == nil || view.window == nil)   return;
    view.window.level = NSFloatingWindowLevel + 1;
}


void MacWindow::bringToFront(QWidget *dialog)
{
    if(dialog == nullptr)   return;
    [NSApp activate];
    raiseAboveFloating(dialog);
    dialog->raise();
    dialog->activateWindow();
}


MacFullScreenOverlay::MacFullScreenOverlay(QObject *parent) : QObject(parent)
{
    //With Hearthstone fullscreen the tracker's windows aren't visible, so App Nap throttled it:
    //finding the draft screen took 30 s instead of 1 s. The activity is held for the app's lifetime.
    static id activity = [[NSProcessInfo processInfo] beginActivityWithOptions:
                            (NSActivityUserInitiated | NSActivityLatencyCritical) reason:@"Tracking Hearthstone"];
    (void)activity;

    qApp->installEventFilter(this);
    connect(&timer, &QTimer::timeout, this, &MacFullScreenOverlay::check);
    timer.start(1000);

    //Check right away on a Space switch, and again once the switch animation is over
    [[NSWorkspace sharedWorkspace].notificationCenter addObserverForName:NSWorkspaceActiveSpaceDidChangeNotification
                                                                  object:nil queue:[NSOperationQueue mainQueue]
                                                              usingBlock:^(NSNotification *) {
        check();
        QTimer::singleShot(500, this, &MacFullScreenOverlay::check);
    }];
}


//Private CoreGraphics Spaces API (used by window managers like yabai): a Space of type 4 is a fullscreen one
extern "C" int CGSMainConnectionID(void);
extern "C" CFArrayRef CGSCopyManagedDisplaySpaces(int connection);


//A Hearthstone window is on screen: in the current Space and not minimized
bool MacFullScreenOverlay::isHearthstoneOnScreen()
{
    bool onScreen = false;

    @autoreleasepool
    {
        CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                        kCGNullWindowID);
        if(windows == nullptr)  return false;
        for(NSDictionary *window in (__bridge NSArray *)windows)
        {
            if([window[(__bridge NSString *)kCGWindowOwnerName] isEqualToString:@"Hearthstone"] &&
                    [window[(__bridge NSString *)kCGWindowLayer] intValue] == 0)
            {
                onScreen = true;
                break;
            }
        }
        CFRelease(windows);
    }
    return onScreen;
}


//The current Space of some display is a fullscreen one. The menu bar can't tell: on displays with a camera
//notch it stays visible over fullscreen apps.
bool MacFullScreenOverlay::isFullScreenSpace()
{
    bool fullScreenSpace = false;

    @autoreleasepool
    {
        CFArrayRef displays = CGSCopyManagedDisplaySpaces(CGSMainConnectionID());
        if(displays == nullptr)     return false;
        for(NSDictionary *display in (__bridge NSArray *)displays)
        {
            NSDictionary *space = display[@"Current Space"];
            if([space[@"type"] intValue] == 4)  fullScreenSpace = true;
        }
        CFRelease(displays);
    }
    return fullScreenSpace;
}


bool MacFullScreenOverlay::isHearthstoneFullScreen()
{
    return isHearthstoneOnScreen() && isFullScreenSpace();
}


bool MacFullScreenOverlay::isDraftOverlay(QWidget *widget)
{
    return widget->inherits("DraftScoreWindow") || widget->inherits("DraftHeroWindow") ||
            widget->inherits("MascotWindow") || widget->inherits("ScorePlatesWindow");
}


//Hidden through the NSWindow alpha, so the draft code keeps showing and hiding them as usual
void MacFullScreenOverlay::showDraftOverlay(QWidget *widget)
{
    NSView *view = nativeView(widget);
    if(view == nil || view.window == nil)   return;
    view.window.alphaValue = hsOnScreen ? 1.0 : 0.0;
    //The plates must let the clicks through to Hearthstone (its options menu opens under them)
    view.window.ignoresMouseEvents = !hsOnScreen || widget->testAttribute(Qt::WA_TransparentForMouseEvents);
}


void MacFullScreenOverlay::check()
{
    bool onScreen = isHearthstoneOnScreen();
    bool changed = (onScreen != hsOnScreen);
    hsOnScreen = onScreen;

    //macOS only lets an accessory app's windows into another app's fullscreen Space
    bool fullScreen = onScreen && isFullScreenSpace();
    if(fullScreen != accessory)
    {
        accessory = fullScreen;
        [NSApp setActivationPolicy:(accessory ? NSApplicationActivationPolicyAccessory : NSApplicationActivationPolicyRegular)];
    }

    if(!changed)    return;
    for(QWidget *widget: QApplication::topLevelWidgets())
    {
        if(widget->isVisible() && isDraftOverlay(widget))   showDraftOverlay(widget);
    }
}


bool MacFullScreenOverlay::eventFilter(QObject *watched, QEvent *event)
{
    //Qt recreates the NSWindow when the window flags change, so the behaviour is set on every show
    if(event->type() == QEvent::Show && watched->isWidgetType())
    {
        QWidget *widget = static_cast<QWidget *>(watched);
        if(widget->isWindow() && widget->windowFlags().testFlag(Qt::WindowStaysOnTopHint))
        {
            NSView *view = nativeView(widget);
            if(view != nil && view.window != nil)
            {
                NSWindowCollectionBehavior behavior = view.window.collectionBehavior;
                behavior &= ~(NSWindowCollectionBehaviorMoveToActiveSpace | NSWindowCollectionBehaviorFullScreenPrimary);
                behavior |= NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
                view.window.collectionBehavior = behavior;

                //A non-activating panel (a Qt::Tool window) gets into another app's fullscreen Space even created
                //before it and while the app is a regular one, which a plain window doesn't
                if([view.window isKindOfClass:[NSPanel class]])
                    view.window.styleMask |= NSWindowStyleMaskNonactivatingPanel;
            }
            if(isDraftOverlay(widget))  showDraftOverlay(widget);
        }
    }
    return QObject::eventFilter(watched, event);
}


MacHoverTracker::MacHoverTracker(QObject *parent) : QObject(parent)
{
    connect(&timer, &QTimer::timeout, this, &MacHoverTracker::check);
    timer.start(30);
}


void MacHoverTracker::check()
{
    //Active: Qt handles the mouse itself
    if(QGuiApplication::applicationState() == Qt::ApplicationActive)
    {
        lastWidget = nullptr;
        return;
    }

    const QPoint pos = QCursor::pos();
    QWidget *widget = QApplication::widgetAt(pos);
    //An overlay hidden with Hearthstone off screen is still visible to Qt (MacFullScreenOverlay only makes it
    //transparent and lets the mouse through): its rows popped up card previews over the desktop
    if(widget != nullptr)
    {
        NSView *view = nativeView(widget->window());
        if(view != nil && view.window != nil && view.window.ignoresMouseEvents)     widget = nullptr;
    }

    if(widget != lastWidget)
    {
        dispatchEnterLeave(widget, lastWidget, pos);
        lastWidget = widget;
    }

    if(widget != nullptr && pos != lastPos && widget->hasMouseTracking())
    {
        QMouseEvent event(QEvent::MouseMove, widget->mapFromGlobal(QPointF(pos)), QPointF(pos),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &event);
    }
    lastPos = pos;
}


//Leave for the old widget and its parents not under the cursor anymore, then enter for the new ones, outer first
void MacHoverTracker::dispatchEnterLeave(QWidget *enter, QWidget *leave, const QPoint &globalPos)
{
    QList<QWidget *> enterChain, leaveChain;
    for(QWidget *w=enter; w!=nullptr; w=(w->isWindow()?nullptr:w->parentWidget()))  enterChain << w;
    for(QWidget *w=leave; w!=nullptr; w=(w->isWindow()?nullptr:w->parentWidget()))  leaveChain << w;
    while(!enterChain.isEmpty() && !leaveChain.isEmpty() && enterChain.last() == leaveChain.last())
    {
        enterChain.removeLast();
        leaveChain.removeLast();
    }

    for(QWidget *w: std::as_const(leaveChain))
    {
        QEvent event(QEvent::Leave);
        QApplication::sendEvent(w, &event);
    }
    for(int i=enterChain.count()-1; i>=0; i--)
    {
        QWidget *w = enterChain[i];
        QEnterEvent event(w->mapFromGlobal(QPointF(globalPos)), w->mapFromGlobal(QPointF(globalPos)), QPointF(globalPos));
        QApplication::sendEvent(w, &event);
    }
}


bool MacWindow::systemDialogOverHearthstone()
{
    CGRect hearthstone = CGRectNull;
    QList<CGRect> dialogs;
    @autoreleasepool
    {
        CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                        kCGNullWindowID);
        if(windows == nullptr)  return false;
        for(NSDictionary *window in (__bridge NSArray *)windows)
        {
            NSString *owner = window[(__bridge NSString *)kCGWindowOwnerName];
            CGRect bounds;
            if(!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)window[(__bridge NSString *)kCGWindowBounds], &bounds))
                continue;
            if([owner isEqualToString:@"Hearthstone"] && [window[(__bridge NSString *)kCGWindowLayer] intValue] == 0)
            {
                if(CGRectIsNull(hearthstone) || bounds.size.width*bounds.size.height > hearthstone.size.width*hearthstone.size.height)
                    hearthstone = bounds;
            }
            else if([owner isEqualToString:@"UserNotificationCenter"])     dialogs << bounds;
        }
        CFRelease(windows);
    }
    if(CGRectIsNull(hearthstone))   return false;
    for(const CGRect &dialog: dialogs)  if(CGRectIntersectsRect(dialog, hearthstone))   return true;
    return false;
}
