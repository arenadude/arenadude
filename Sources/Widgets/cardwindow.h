#ifndef CARDWINDOW_H
#define CARDWINDOW_H

#include <QMainWindow>
#include <QObject>
#include <QLabel>
#include <QHash>

#define HCARD 254
#define WCARD 182

class CardWindow : public QMainWindow
{
    Q_OBJECT

//Constructor
public:
    CardWindow(QWidget *parent);
    ~CardWindow() Q_DECL_OVERRIDE;

//Variables
private:
    QLabel *cardLabel;
    bool alwaysHidden;
    QHash<QString, QRect> cardBounds;       //Opaque area of each card image, by code or HD file

    static QRect opaqueBounds(const QImage &image);

//Metodos
protected:
    void leaveEvent(QEvent *e) Q_DECL_OVERRIDE;
    void enterEvent(QEnterEvent *e) Q_DECL_OVERRIDE;

signals:

public slots:
    void loadCard(QString code, QRect rectCard, int maxTop, int maxBottom, bool alignReverse=false);
    void scale(int value_x10);
};

#endif // CARDWINDOW_H
