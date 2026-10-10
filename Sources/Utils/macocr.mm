#include "macocr.h"
#include "replayscreen.h"
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#import <CoreGraphics/CoreGraphics.h>

QStringList MacOcr::recognizeLines(const QImage &image, const QString &language)
{
    QStringList lines;
    for(const TextLine &line: recognizeTextLines(image, language))  lines << line.text;
    return lines;
}


QList<MacOcr::TextLine> MacOcr::recognizeTextLines(const QImage &image, const QString &language, bool fast)
{
    QList<TextLine> lines;
    if(image.isNull())  return lines;

    @autoreleasepool
    {
        CGImageRef cgImage = image.toCGImage();
        if(cgImage == nullptr)  return lines;

        VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
        request.recognitionLevel = fast ? VNRequestTextRecognitionLevelFast : VNRequestTextRecognitionLevelAccurate;
        //Card names are not dictionary words
        request.usesLanguageCorrection = NO;
        //enUS --> en-US
        if(language.length() == 4)
        {
            QString bcp47 = language.left(2) + "-" + language.right(2);
            request.recognitionLanguages = @[bcp47.toNSString()];
        }

        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCGImage:cgImage options:@{}];
        NSError *error = nil;
        if([handler performRequests:@[request] error:&error])
        {
            const qreal w = image.width(), h = image.height();
            for(VNRecognizedTextObservation *observation in request.results)
            {
                VNRecognizedText *text = [[observation topCandidates:1] firstObject];
                if(text == nil) continue;
                //Vision boxes are normalized with a bottom-left origin
                CGRect box = observation.boundingBox;
                lines << TextLine{QString::fromNSString(text.string),
                                  QRectF(box.origin.x*w, (1 - box.origin.y - box.size.height)*h,
                                         box.size.width*w, box.size.height*h)};
            }
        }
        CGImageRelease(cgImage);
    }
    return lines;
}


QRect MacOcr::hearthstoneWindowRect()
{
    if(ReplayScreen::isActive())    return ReplayScreen::hearthstoneRect();
    QRect bestRect;

    @autoreleasepool
    {
        CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                        kCGNullWindowID);
        if(windows == nullptr)  return bestRect;

        for(NSDictionary *window in (__bridge NSArray *)windows)
        {
            if(![window[(__bridge NSString *)kCGWindowOwnerName] isEqualToString:@"Hearthstone"])  continue;
            if([window[(__bridge NSString *)kCGWindowLayer] intValue] != 0)                        continue;

            CGRect bounds;
            if(!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)window[(__bridge NSString *)kCGWindowBounds], &bounds))
                continue;
            QRect rect(bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height);
            if(rect.width()*rect.height() > bestRect.width()*bestRect.height())    bestRect = rect;
        }
        CFRelease(windows);
    }
    return bestRect;
}
