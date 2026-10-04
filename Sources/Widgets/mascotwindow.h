#ifndef MASCOTWINDOW_H
#define MASCOTWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QPainter>
#include <QTimer>
#include <QPoint>
#include <QTextDocument>
#include <functional>


//The mascot: a draggable pixel character that tells the tracker's status and advice in a speech bubble.
//The window grows upwards when the bubble shows, so the character stays where the user left it.
class MascotWindow : public QWidget
{
    Q_OBJECT
public:
    enum Mood { Idle, Popcorn, Thinking, Point, Smile, Grin, Smug, Happy, Sweat, Grabbed, Stars, Detective, Blind, NumMoods };

    //A section of name/value rows under the text (e.g. the cards to remove by one score source).
    //Rows with a card code show the card when hovered.
    struct Row
    {
        QString name, value, code;
        QColor nameColor = Qt::black;
    };
    struct Section
    {
        QString header;
        QList<Row> rows;
    };

    explicit MascotWindow(QWidget *parent = nullptr);

    //The mascot's look for other overlays: a box with a black frame of square pixels, and the pixel font
    static void drawPixelFrame(QPainter &painter, const QRect &rect, const QColor &fill);
    static QFont pixelFont(int pixelSize);
    //A part of the bubble text drawn in its own color (e.g. a card name in its rarity color)
    static QString colored(const QString &text, const QColor &color);

    void setMood(Mood mood);
    Mood currentMood() const { return mood; }
    //The moods with frames (<mood>_0.png, <mood>_1.png...) play them instead of their still sprite
    void setAnimated(bool animated);
    //An empty text hides the bubble. With msec > 0 the bubble hides by itself after that time.
    //With a button text, the bubble shows a button that runs the action (and hides the bubble).
    void say(const QString &text, int msec = 0, const QString &button = QString(), std::function<void()> action = nullptr);
    //Sections one under the other, under the text
    void saySections(const QString &text, const QList<Section> &sections, int msec = 0,
                     const QString &button = QString(), std::function<void()> action = nullptr);

signals:
    void quitRequested();
    void discordRequested();
    void supportRequested();
    void reportRequested();     //Report a problem: the log and the Discord
    void said(const QString &text);     //For the log
    void cardEntered(QString code, QRect rectCard, int maxTop, int maxBottom);
    void cardLeave();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QPixmap sprites[NumMoods];
    //Animation: the frames of a mood and the order they play in, (frame, msec) steps in a loop
    QList<QPixmap> frames[NumMoods];
    QList<QPair<int, int>> frameSteps[NumMoods];
    bool animated = false;
    int frameStep = 0;
    QTimer frameTimer;
    Mood mood = Idle;
    Mood moodBeforeDrag = Idle;
    QString text, buttonText;
    QList<Section> sections;
    QList<QRect> headerRects;
    QList<QList<QRect>> rowRects;       //By section
    int hoveredSection = -1, hoveredRow = -1;
    std::function<void()> buttonAction;
    QTimer sayTimer;
    int pendingSayMsec = 0;     //A timed line said while hidden
    QFont bubbleFont;
    QRect bubbleRect, textRect, buttonRect, spriteRect;
    QPoint anchor;              //Global position of the character's bottom center
    QPoint dragOffset, pressPos;
    bool dragging = false, dragMoved = false, buttonPressed = false;

    void relayout();
    void setupTextDocument(QTextDocument &doc, int width) const;
    void drawBubble(QPainter &painter);
    void drawSections(QPainter &painter);
    void updateHover(const QPoint &pos);
    void applyCursor(Qt::CursorShape shape);
    void loadAnchor();
    void saveAnchor();
};

#endif // MASCOTWINDOW_H
