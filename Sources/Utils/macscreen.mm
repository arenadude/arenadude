#include "macscreen.h"
#include <QDateTime>
#include <QMutex>
#import <ScreenCaptureKit/ScreenCaptureKit.h>


namespace
{
    //Looking the window up costs more than the capture: done again after a second, or when a capture fails
    QMutex windowMutex;
    SCWindow *cachedWindow = nil;
    qint64 cachedWindowTime = 0;
    const qint64 WINDOW_CACHE_MS = 1000;
    const int WAIT_MS = 2000;

    SCWindow *findHearthstoneWindow()
    {
        __block SCShareableContent *shareable = nil;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:YES
                                                     completionHandler:^(SCShareableContent *content, NSError *error) {
            if(error == nil)    shareable = content;
            dispatch_semaphore_signal(done);
        }];
        if(dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, WAIT_MS*NSEC_PER_MSEC)) != 0)    return nil;

        SCWindow *best = nil;
        for(SCWindow *window in shareable.windows)
        {
            if(![window.owningApplication.applicationName isEqualToString:@"Hearthstone"] || window.windowLayer != 0)
                continue;
            if(best == nil || window.frame.size.width*window.frame.size.height > best.frame.size.width*best.frame.size.height)
                best = window;
        }
        return best;
    }

    SCWindow *hearthstoneScWindow(bool refresh)
    {
        QMutexLocker locker(&windowMutex);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if(refresh || cachedWindow == nil || now - cachedWindowTime > WINDOW_CACHE_MS)
        {
            cachedWindow = findHearthstoneWindow();
            cachedWindowTime = now;
        }
        return cachedWindow;
    }

    CGImageRef capture(SCWindow *window)
    {
        SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
        SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
        config.width = static_cast<size_t>(filter.contentRect.size.width * filter.pointPixelScale);
        config.height = static_cast<size_t>(filter.contentRect.size.height * filter.pointPixelScale);
        config.showsCursor = NO;
        config.ignoreShadowsSingleWindow = YES;

        __block CGImageRef result = nullptr;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [SCScreenshotManager captureImageWithFilter:filter configuration:config
                                  completionHandler:^(CGImageRef image, NSError *error) {
            if(error == nil && image != nullptr)    result = CGImageRetain(image);
            dispatch_semaphore_signal(done);
        }];
        //A late image after the timeout is leaked: rare, and a release from here would race the handler
        if(dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, WAIT_MS*NSEC_PER_MSEC)) != 0)   return nullptr;
        return result;
    }

    QImage toQImage(CGImageRef cgImage)
    {
        QImage image(static_cast<int>(CGImageGetWidth(cgImage)), static_cast<int>(CGImageGetHeight(cgImage)),
                     QImage::Format_ARGB32_Premultiplied);
        CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef context = CGBitmapContextCreate(image.bits(), image.width(), image.height(), 8, image.bytesPerLine(),
                                                     colorSpace, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGContextDrawImage(context, CGRectMake(0, 0, image.width(), image.height()), cgImage);
        CGContextRelease(context);
        CGColorSpaceRelease(colorSpace);
        return image;
    }
}


MacScreen::Result MacScreen::hearthstoneWindow(QImage &image, QRect &frame)
{
    @autoreleasepool
    {
        for(int attempt=0; attempt<2; attempt++)
        {
            //A failed capture may be a closed or recreated window: looked up again once
            SCWindow *window = hearthstoneScWindow(attempt > 0);
            if(window == nil)   return NoWindow;
            CGImageRef cgImage = capture(window);
            if(cgImage == nullptr)  continue;
            image = toQImage(cgImage);
            CGImageRelease(cgImage);
            const CGRect bounds = window.frame;
            frame = QRect(qRound(bounds.origin.x), qRound(bounds.origin.y), qRound(bounds.size.width), qRound(bounds.size.height));
            return Captured;
        }
    }
    return Failed;
}
