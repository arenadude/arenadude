#ifndef SPLASHWINDOW_H
#define SPLASHWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QTimer>
#include <QElapsedTimer>


//The start screen: the mascot blowing the dust off the game box, "Starting..." in a pixel bubble, and on the first
//run a pixel progress bar of the card images download. It closes when the tracker is ready, or when the download
//stops moving (offline): the images keep downloading in the background.
class SplashWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SplashWindow(QWidget *parent = nullptr);

public slots:
    void setProgress(int done, int total);
    void ready();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap art;
    QSize artSize;
    int done = 0, total = 0;
    bool readySeen = false;
    QTimer stallTimer, lineTimer;
    int lineIndex = 0;
    QElapsedTimer shownClock;

    void closeWhenShownEnough();
};

#endif // SPLASHWINDOW_H
