#ifndef HDIMAGES_H
#define HDIMAGES_H

#include <QObject>
#include <QSet>
#include <QNetworkAccessManager>
#include "../utility.h"

//High resolution images from HearthstoneJSON, downloaded when first needed and kept in "HD Images".
//The card images of the tracker (200x304) look blurry on Retina screens and are also used to recognize
//the draft cards, so these only replace them on screen.
class HDImages : public QObject
{
    Q_OBJECT
public:
    enum Kind { Tile, Render, Portrait };

    static void create(QObject *parent);
    //Local file of the image, or empty: then it is downloaded and ready() comes after
    static QString path(Kind kind, const QString &code);

private:
    explicit HDImages(QObject *parent);
    static HDImages *self;
    QNetworkAccessManager networkManager;
    QSet<QString> requested;    //Kind/code already downloading or failed

    QString filePath(Kind kind, const QString &code);
    void request(Kind kind, const QString &code);

signals:
    void ready(int kind, QString code);
    void pDebug(QString line, DebugLevel debugLevel=Normal, QString file="HDImages");
};

#endif // HDIMAGES_H
