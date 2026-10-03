#macOS only

QT       += core gui network widgets

TARGET = ArenaDude
TEMPLATE = app

QT_CONFIG -= no-pkg-config

CONFIG += link_pkgconfig
#Only the OpenCV modules used: pkg-config links all of them, and the app bundle would carry them (and their
#dependencies: video codecs, VTK...) for nothing
QMAKE_CXXFLAGS += $$system(pkg-config --cflags opencv5)
LIBS += -L$$system(pkg-config --variable=libdir opencv5) \
        -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_features -lopencv_geometry -lopencv_flann
PKGCONFIG += libzip
LIBS += -lz

SOURCES += Sources/main.cpp\
    Sources/mainwindow.cpp \
    Sources/logloader.cpp \
    Sources/logworker.cpp \
    Sources/gamewatcher.cpp \
    Sources/hscarddownloader.cpp \
    Sources/deckhandler.cpp \
    Sources/arenahandler.cpp \
    Sources/drafthandler.cpp \
    Sources/utility.cpp \
    Sources/Cards/deckcard.cpp \
    Sources/Cards/draftcard.cpp \
    Sources/Widgets/draftscorewindow.cpp \
    Sources/Widgets/splashwindow.cpp \
    Sources/Utils/pickrating.cpp \
    Sources/Widgets/mascotwindow.cpp \
    Sources/Widgets/scoreplate.cpp \
    Sources/Widgets/cardwindow.cpp \
    Sources/versionchecker.cpp \
    Sources/Utils/libzippp.cpp \
    Sources/Utils/hdimages.cpp \
    Sources/Utils/hdicons.cpp \
    Sources/Widgets/draftherowindow.cpp \
    Sources/winratesdownloader.cpp

HEADERS  += Sources/mainwindow.h \
    Sources/logloader.h \
    Sources/logworker.h \
    Sources/gamewatcher.h \
    Sources/hscarddownloader.h \
    Sources/deckhandler.h \
    Sources/arenahandler.h \
    Sources/drafthandler.h \
    Sources/utility.h \
    Sources/Cards/deckcard.h \
    Sources/Cards/draftcard.h \
    Sources/Widgets/draftscorewindow.h \
    Sources/Widgets/splashwindow.h \
    Sources/Utils/pickrating.h \
    Sources/Widgets/mascotwindow.h \
    Sources/Widgets/scoreplate.h \
    Sources/Widgets/cardwindow.h \
    Sources/versionchecker.h \
    Sources/constants.h \
    Sources/Utils/libzippp.h \
    Sources/Utils/hdimages.h \
    Sources/Utils/hdicons.h \
    Sources/Widgets/draftherowindow.h \
    Sources/winratesdownloader.h


RESOURCES += \
    arenadude.qrc

ICON = ArenaDude.icns
QMAKE_TARGET_BUNDLE_PREFIX = com.arenadude
LIBS += -liconv
OBJECTIVE_SOURCES += Sources/Utils/macocr.mm Sources/Utils/macwindow.mm
HEADERS  += Sources/Utils/macocr.h Sources/Utils/macwindow.h
LIBS += -framework Foundation -framework Vision -framework CoreGraphics -framework AppKit
QMAKE_OBJECTIVE_CFLAGS += -fobjc-arc
