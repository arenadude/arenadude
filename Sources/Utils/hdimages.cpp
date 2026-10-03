#include "hdimages.h"
#include <QtWidgets>
#include <QNetworkReply>

HDImages *HDImages::self = nullptr;


HDImages::HDImages(QObject *parent) : QObject(parent)
{
}


void HDImages::create(QObject *parent)
{
    if(self == nullptr)     self = new HDImages(parent);
}


QString HDImages::filePath(Kind kind, const QString &code)
{
    QString dir = Utility::dataPath() + "/HD Images/";
    switch(kind)
    {
        case Tile:      return dir + "Tiles/" + code + ".png";
        case Render:    return dir + "Cards " + Utility::getLocalLang() + "/" + code + ".png";
        case Portrait:  return dir + "Portraits/" + code + ".jpg";
    }
    return QString();
}


QString HDImages::path(Kind kind, const QString &code)
{
    if(self == nullptr || code.isEmpty())   return QString();

    QString file = self->filePath(kind, code);
    if(QFileInfo::exists(file))     return file;
    self->request(kind, code);
    return QString();
}


void HDImages::request(Kind kind, const QString &code)
{
    const QString key = QString::number(kind) + "/" + code;
    if(requested.contains(key))     return;
    requested.insert(key);

    QString url = "https://art.hearthstonejson.com/v1/";
    switch(kind)
    {
        case Tile:      url += "tiles/" + code + ".png";    break;
        case Render:    url += "render/latest/" + Utility::getLocalLang() + "/512x/" + code + ".png";    break;
        case Portrait:  url += "256x/" + code + ".jpg";     break;
    }

    QNetworkReply *reply = networkManager.get(QNetworkRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, kind, code, key]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        //Failed ones stay in requested: not asked again in this session
        if(reply->error() != QNetworkReply::NoError || data.isEmpty() || QImage::fromData(data).isNull())
        {
            emit pDebug("HD image not available: " + reply->url().toString());
            return;
        }

        QString file = filePath(kind, code);
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile out(file);
        if(!out.open(QIODevice::WriteOnly))     return;
        out.write(data);
        out.close();
        requested.remove(key);
        emit ready(kind, code);
    });
}
