#include "mascotwindow.h"
#include <QtWidgets>
    #include "../Utils/macwindow.h"


#define MASCOT_SPRITE_HEIGHT    163     //Points; the sprites have room above the hat for the grabbed one
#define MASCOT_BUBBLE_MAX_WIDTH 260
#define MASCOT_PIXEL            3       //Size of one "pixel" of the bubble frame
#define MASCOT_BUBBLE_PADDING   9
#define MASCOT_TAIL_HEIGHT      15      //5 frame pixels
#define MASCOT_FONT_SIZE        20      //Jersey 10 is drawn on a 10 px grid
#define MASCOT_BUTTON_HEIGHT    29
#define MASCOT_BUTTON_GAP       8
#define MASCOT_PURPLE           QColor(107, 27, 155)    //The hat's purple
#define MASCOT_HOVER_COLOR      QColor(236, 226, 244)
#define MASCOT_NAME_MAX         200     //Longer card names are elided
#define MASCOT_VALUE_GAP        14
#define MASCOT_BLOCK_GAP        8       //Between the text, the sections and the button
#define MASCOT_SECTION_GAP      6
#define MASCOT_COLOR_BEGIN      QChar(0x01)     //Followed by the color name (#rrggbb), the text and MASCOT_COLOR_END
#define MASCOT_COLOR_END        QChar(0x02)


MascotWindow::MascotWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint)
{
    //A tool window is a panel on macOS: made non-activating (MacFullScreenOverlay) it shows over fullscreen Hearthstone.
    //Tool windows hide when the app is inactive unless told otherwise.
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);     //Hovered cards and cursors, also while Hearthstone is the active app (MacHoverTracker)
    setWindowTitle("AT Mascot");

    //Moods without their own art yet use a close one
    const char *files[NumMoods] = {"idle", "popcorn", "thinking", "point", "smile", "grin", "smug", "happy", "sweat",
                                   "grabbed", "stars", "detective", "blind"};
    const Mood fallbacks[NumMoods] = {Idle, Popcorn, Thinking, Point, Smile, Grin, Smug, Happy, Sweat, Sweat, Happy, Thinking, Sweat};
    for(int i=0; i<NumMoods; i++)   sprites[i] = QPixmap(QStringLiteral(":/Images/Mascot/%1.png").arg(files[i]));
    for(int i=0; i<NumMoods; i++)   if(sprites[i].isNull())     sprites[i] = sprites[fallbacks[i]];

    bubbleFont = pixelFont(MASCOT_FONT_SIZE);

    sayTimer.setSingleShot(true);
    connect(&sayTimer, &QTimer::timeout, this, [this]() { say(""); });

    loadAnchor();
    relayout();
}


void MascotWindow::setMood(Mood mood)
{
    if(dragging && dragMoved)
    {
        moodBeforeDrag = mood;      //Shown when it's dropped
        return;
    }
    if(this->mood == mood)  return;
    this->mood = mood;
    update();
}


void MascotWindow::say(const QString &text, int msec, const QString &button, std::function<void()> action)
{
    saySections(text, {}, msec, button, action);
}


void MascotWindow::saySections(const QString &text, const QList<Section> &sections, int msec,
                               const QString &button, std::function<void()> action)
{
    //While the mascot is hidden (Hearthstone not on screen) the time starts when it shows
    sayTimer.stop();
    pendingSayMsec = 0;
    if(msec > 0 && !text.isEmpty())
    {
        if(isVisible()) sayTimer.start(msec);
        else            pendingSayMsec = msec;
    }
    if(hoveredRow != -1)    emit cardLeave();
    hoveredSection = hoveredRow = -1;
    this->sections = text.isEmpty() ? QList<Section>() : sections;
    buttonAction = text.isEmpty() ? nullptr : action;
    buttonText = text.isEmpty() ? QString() : button;
    this->text = text;
    if(!text.isEmpty())
    {
        QString plain = text;
        emit said(plain.remove(QRegularExpression("\\x01#[0-9a-f]{6}|\\x02")).replace("\n", " "));
    }
    relayout();
    update();
}


//Bubble on top, the character under it. The window is placed so the character's bottom center is on the anchor.
void MascotWindow::relayout()
{
    const QPixmap &sprite = sprites[Idle];
    int spriteW = sprite.isNull() ? MASCOT_SPRITE_HEIGHT : MASCOT_SPRITE_HEIGHT * sprite.width() / sprite.height();
    int spriteH = MASCOT_SPRITE_HEIGHT;
    const int inset = MASCOT_BUBBLE_PADDING + MASCOT_PIXEL;
    QFontMetrics fm(bubbleFont);
    const int lineH = fm.height();

    QSize bubbleSize(0, 0);
    QRect textBox, buttonBox;
    headerRects.clear();
    rowRects.clear();
    if(!text.isEmpty())
    {
        //Sections: as wide as the widest header or row; relative to the content, placed under the text below
        int sectionsW = 0, sectionsH = 0;
        for(const Section &section: std::as_const(sections))
        {
            sectionsW = std::max(sectionsW, fm.horizontalAdvance(section.header));
            for(const Row &row: section.rows)
            {
                int nameW = std::min(fm.horizontalAdvance(row.name), MASCOT_NAME_MAX);
                sectionsW = std::max(sectionsW, nameW + MASCOT_VALUE_GAP + fm.horizontalAdvance(row.value));
            }
        }
        for(const Section &section: std::as_const(sections))
        {
            if(sectionsH > 0)   sectionsH += MASCOT_SECTION_GAP;
            headerRects << QRect(0, sectionsH, sectionsW, lineH);
            sectionsH += lineH;
            QList<QRect> rects;
            for(int i=0; i<section.rows.count(); i++)
            {
                rects << QRect(0, sectionsH, sectionsW, lineH);
                sectionsH += lineH;
            }
            rowRects << rects;
        }

        int maxTextW = std::max(MASCOT_BUBBLE_MAX_WIDTH - 2*inset, sectionsW);
        QTextDocument doc;
        setupTextDocument(doc, maxTextW);
        textBox = QRect(0, 0, qCeil(doc.idealWidth()), qCeil(doc.size().height()));
        int contentW = std::max(textBox.width(), sectionsW);
        int contentH = textBox.height();
        if(sectionsH > 0)   contentH += MASCOT_BLOCK_GAP + sectionsH;
        if(!buttonText.isEmpty())
        {
            buttonBox = QRect(0, 0, fm.horizontalAdvance(buttonText) + 4*MASCOT_PIXEL + 2*MASCOT_BUBBLE_PADDING, MASCOT_BUTTON_HEIGHT);
            contentW = std::max(contentW, buttonBox.width());
            contentH += MASCOT_BUTTON_GAP + MASCOT_BUTTON_HEIGHT;
        }
        bubbleSize = QSize(contentW + 2*inset, contentH + 2*inset);
    }

    int w = std::max(spriteW, bubbleSize.width());
    int bubbleBlockH = text.isEmpty() ? 0 : bubbleSize.height() + MASCOT_TAIL_HEIGHT;
    int h = bubbleBlockH + spriteH;

    bubbleRect = QRect((w - bubbleSize.width())/2, 0, bubbleSize.width(), bubbleSize.height());
    textRect = QRect(bubbleRect.x() + inset, bubbleRect.y() + inset, bubbleSize.width() - 2*inset, textBox.height());
    int blockBottom = textRect.bottom();
    QPoint sectionsOrigin(textRect.x(), textRect.bottom() + 1 + MASCOT_BLOCK_GAP);
    for(QRect &rect: headerRects)
    {
        rect.translate(sectionsOrigin);
        blockBottom = std::max(blockBottom, rect.bottom());
    }
    for(QList<QRect> &rects: rowRects)
    {
        for(QRect &rect: rects)
        {
            rect.translate(sectionsOrigin);
            blockBottom = std::max(blockBottom, rect.bottom());
        }
    }
    buttonRect = buttonText.isEmpty() ? QRect() :
                 QRect(bubbleRect.x() + inset, blockBottom + 1 + MASCOT_BUTTON_GAP, buttonBox.width(), MASCOT_BUTTON_HEIGHT);
    spriteRect = QRect((w - spriteW)/2, bubbleBlockH, spriteW, spriteH);

    setFixedSize(w, h);
    move(anchor.x() - w/2, anchor.y() - h);

    //Clicks go through the empty parts of the window
    QRegion region(spriteRect);
    if(!text.isEmpty())     region += QRegion(bubbleRect.adjusted(0, 0, 0, MASCOT_TAIL_HEIGHT));
    setMask(region);
}


QString MascotWindow::colored(const QString &text, const QColor &color)
{
    return MASCOT_COLOR_BEGIN + color.name() + text + MASCOT_COLOR_END;
}


//The text as HTML: escaped, line breaks kept and the colored() parts in their color
void MascotWindow::setupTextDocument(QTextDocument &doc, int width) const
{
    QString html;
    int from = 0;
    while(from < text.length())
    {
        int begin = text.indexOf(MASCOT_COLOR_BEGIN, from);
        int end = (begin == -1) ? -1 : text.indexOf(MASCOT_COLOR_END, begin);
        if(end == -1)
        {
            html += text.mid(from).toHtmlEscaped();
            break;
        }
        html += text.mid(from, begin - from).toHtmlEscaped();
        const QString color = text.mid(begin + 1, 7);
        html += "<span style=\"color:" + color + "\">" + text.mid(begin + 8, end - begin - 8).toHtmlEscaped() + "</span>";
        from = end + 1;
    }
    html.replace("\n", "<br>");

    doc.setDocumentMargin(0);
    doc.setDefaultFont(bubbleFont);
    doc.setHtml(html);
    doc.setTextWidth(width);
}


void MascotWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if(!text.isEmpty())     drawBubble(painter);
    painter.drawPixmap(spriteRect, sprites[mood]);
}


//Always above the tracker's own stay on top windows, like the old main window opened from its menu
void MascotWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if(pendingSayMsec > 0)
    {
        sayTimer.start(pendingSayMsec);
        pendingSayMsec = 0;
    }
    QTimer::singleShot(0, this, [this]() { MacWindow::raiseAboveFloating(this); });
}


//Jersey 10, loaded once
QFont MascotWindow::pixelFont(int pixelSize)
{
    static QString family;
    if(family.isEmpty())
    {
        int fontId = QFontDatabase::addApplicationFont(":/Fonts/Jersey10.ttf");
        family = QFontDatabase::applicationFontFamilies(fontId).value(0);
    }
    QFont font(family);
    font.setPixelSize(pixelSize);
    return font;
}


//A box with a black frame made of square pixels and notched corners
void MascotWindow::drawPixelFrame(QPainter &painter, const QRect &r, const QColor &fill)
{
    const int p = MASCOT_PIXEL;
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRect(r.adjusted(p, p, -p, -p));
    painter.setBrush(Qt::black);
    painter.drawRect(r.x() + p, r.y(), r.width() - 2*p, p);                     //Top
    painter.drawRect(r.x() + p, r.bottom() - p + 1, r.width() - 2*p, p);        //Bottom
    painter.drawRect(r.x(), r.y() + p, p, r.height() - 2*p);                    //Left
    painter.drawRect(r.right() - p + 1, r.y() + p, p, r.height() - 2*p);        //Right
}


//White box with a stepped tail towards the head, the text, the sections and an optional button
void MascotWindow::drawBubble(QPainter &painter)
{
    const int p = MASCOT_PIXEL;
    const QRect r = bubbleRect;
    drawPixelFrame(painter, r, Qt::white);

    //Tail: a stepped triangle under the bubble, a bit left of its center, pointing at the head
    int x0 = r.x() + r.width()/2 - 6*p;
    int y0 = r.bottom() + 1;
    painter.setBrush(Qt::white);
    painter.drawRect(x0 + p, y0 - p, 7*p, p);                       //Opening in the bottom border
    for(int k=0; k<4; k++)
    {
        int left = x0 + k*p, right = x0 + (8 - k)*p, y = y0 + k*p;
        painter.setBrush(Qt::black);
        painter.drawRect(left, y, p, p);
        painter.drawRect(right, y, p, p);
        painter.setBrush(Qt::white);
        painter.drawRect(left + p, y, right - left - p, p);
    }
    painter.setBrush(Qt::black);
    painter.drawRect(x0 + 4*p, y0 + 4*p, p, p);                     //Tip

    //Black whatever the system palette is
    QTextDocument doc;
    setupTextDocument(doc, textRect.width());
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, Qt::black);
    painter.save();
    painter.translate(textRect.topLeft());
    doc.documentLayout()->draw(&painter, context);
    painter.restore();

    painter.setPen(Qt::black);
    painter.setFont(bubbleFont);
    drawSections(painter);

    if(!buttonText.isEmpty())
    {
        QRect button = buttonRect.translated(0, buttonPressed ? p : 0);
        if(!buttonPressed)  drawPixelFrame(painter, buttonRect.translated(0, p), Qt::black);     //Shadow
        drawPixelFrame(painter, button, MASCOT_PURPLE);
        painter.setPen(Qt::white);
        painter.drawText(button, Qt::AlignCenter, buttonText);
    }
}


//Header in the hat's purple, then name (elided, in its color) on the left and value (grey) on the right of each row
void MascotWindow::drawSections(QPainter &painter)
{
    QFontMetrics fm(bubbleFont);
    for(int s=0; s<sections.count() && s<headerRects.count(); s++)
    {
        painter.setPen(MASCOT_PURPLE);
        painter.drawText(headerRects[s], Qt::AlignLeft | Qt::AlignVCenter, sections[s].header);
        for(int i=0; i<sections[s].rows.count() && i<rowRects[s].count(); i++)
        {
            const Row &row = sections[s].rows[i];
            const QRect &r = rowRects[s][i];
            if(s == hoveredSection && i == hoveredRow)
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(MASCOT_HOVER_COLOR);
                painter.drawRect(r.adjusted(-MASCOT_PIXEL, 0, MASCOT_PIXEL, 0));
            }
            int valueW = fm.horizontalAdvance(row.value);
            //A couple of pixels of slack: elidedText cuts names that horizontalAdvance said fit
            QString name = fm.elidedText(row.name, Qt::ElideRight, r.width() - valueW - MASCOT_VALUE_GAP + 2);
            painter.setPen(row.nameColor);
            painter.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, name);
            painter.setPen(QColor(100, 100, 100));
            painter.drawText(r, Qt::AlignRight | Qt::AlignVCenter, row.value);
        }
    }
}


//The hovered card row shows its card; the cursor tells what a click does
void MascotWindow::updateHover(const QPoint &pos)
{
    int section = -1, row = -1;
    for(int s=0; s<rowRects.count() && row == -1; s++)
    {
        for(int i=0; i<rowRects[s].count(); i++)
        {
            if(rowRects[s][i].contains(pos) && !sections[s].rows[i].code.isEmpty())
            {
                section = s;
                row = i;
                break;
            }
        }
    }

    if(section != hoveredSection || row != hoveredRow)
    {
        hoveredSection = section;
        hoveredRow = row;
        if(row == -1)   emit cardLeave();
        else
        {
            QRect r = rowRects[section][row];
            QRect global(mapToGlobal(QPoint(bubbleRect.left(), r.top())), QSize(bubbleRect.width(), r.height()));
            emit cardEntered(sections[section].rows[row].code, global, -1, -1);
        }
        update();
    }

    if(row != -1 || buttonRect.contains(pos))   applyCursor(Qt::PointingHandCursor);
    else if(spriteRect.contains(pos))           applyCursor(Qt::OpenHandCursor);
    else                                        applyCursor(Qt::ArrowCursor);
}


void MascotWindow::applyCursor(Qt::CursorShape shape)
{
    if(cursor().shape() != shape)   setCursor(shape);
    //Qt only sets it while the app is active, and Hearthstone usually is
    MacWindow::setCursorNow(shape);
}


void MascotWindow::mousePressEvent(QMouseEvent *event)
{
    if(event->button() != Qt::LeftButton)   return;
    QPoint pos = event->position().toPoint();
    if(buttonRect.contains(pos))
    {
        buttonPressed = true;
        update();
        return;
    }
    if(!spriteRect.contains(pos) && !bubbleRect.contains(pos))  return;
    dragging = true;
    dragMoved = false;
    pressPos = event->globalPosition().toPoint();
    dragOffset = pressPos - anchor;
    applyCursor(Qt::ClosedHandCursor);
}


void MascotWindow::mouseMoveEvent(QMouseEvent *event)
{
    if(!dragging)
    {
        updateHover(event->position().toPoint());
        return;
    }
    QPoint pos = event->globalPosition().toPoint();
    if(!dragMoved && (pos - pressPos).manhattanLength() > 3)
    {
        //Lifted by the scruff
        moodBeforeDrag = mood;
        setMood(Grabbed);
        dragMoved = true;
        if(hoveredRow != -1)
        {
            hoveredSection = hoveredRow = -1;
            emit cardLeave();
        }
    }
    anchor = pos - dragOffset;
    move(anchor.x() - width()/2, anchor.y() - height());
    applyCursor(Qt::ClosedHandCursor);
}


void MascotWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if(event->button() != Qt::LeftButton)   return;
    if(buttonPressed)
    {
        buttonPressed = false;
        update();
        if(buttonRect.contains(event->position().toPoint()) && buttonAction)
        {
            std::function<void()> action = buttonAction;
            say("");
            action();
        }
        return;
    }
    if(!dragging)   return;
    dragging = false;
    if(dragMoved)
    {
        dragMoved = false;
        Mood restore = moodBeforeDrag;
        mood = Grabbed;
        setMood(restore);
        saveAnchor();
    }
    updateHover(event->position().toPoint());
}


void MascotWindow::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if(dragging)    return;
    if(hoveredRow != -1)
    {
        hoveredSection = hoveredRow = -1;
        emit cardLeave();
        update();
    }
    applyCursor(Qt::ArrowCursor);
}


//A pixel icon from rows of '#', in the mascot's pixel style; drawn 2x for retina screens, no smoothing
static QIcon pixelIcon(const QStringList &rows, const QColor &color)
{
    const int size = rows.size();
    QImage image(size*2, size*2, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    for(int y=0; y<size; y++)
    {
        for(int x=0; x<rows[y].size(); x++)
        {
            if(rows[y][x] != '#')   continue;
            for(int dy=0; dy<2; dy++)   for(int dx=0; dx<2; dx++)   image.setPixelColor(x*2 + dx, y*2 + dy, color);
        }
    }
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(2);
    return QIcon(pixmap);
}


//Next to the character, so the menu doesn't cover it
void MascotWindow::contextMenuEvent(QContextMenuEvent *event)
{
    //Discord's Clyde and Patreon's mark, in their brand colors
    static const QIcon discordIcon = pixelIcon({
        "................",
        "................",
        "...###....###...",
        "..############..",
        ".##############.",
        ".##############.",
        "####..####..####",
        "####..####..####",
        "####..####..####",
        "################",
        ".##############.",
        ".###........###.",
        "..#..........#..",
        "................",
        "................",
        "................"}, QColor(0x58, 0x65, 0xF2));
    static const QIcon patreonIcon = pixelIcon({
        "................",
        "................",
        ".###.....####...",
        ".###...########.",
        ".###..#########.",
        ".###.##########.",
        ".###.###########",
        ".###.###########",
        ".###.###########",
        ".###.##########.",
        ".###..#########.",
        ".###...#######..",
        ".###.....###....",
        ".###............",
        "................",
        "................"}, QColor(0xFF, 0x42, 0x4D));

    //Qt hides icons in macOS menus unless asked
    QMenu menu(this);
    menu.addAction(discordIcon, "Join Discord", this, &MascotWindow::discordRequested)->setIconVisibleInMenu(true);
    menu.addAction(patreonIcon, "Support on Patreon", this, &MascotWindow::supportRequested)->setIconVisibleInMenu(true);
    //An empty icon keeps the other items' text in line with the iconed ones
    static const QIcon noIcon = pixelIcon(QStringList(16, QString(16, '.')), Qt::transparent);
    menu.addAction(noIcon, "Report a problem", this, &MascotWindow::reportRequested)->setIconVisibleInMenu(true);

    //The native separator is barely visible on the dark menu: a line in the menu's text color, faded, for both themes
    QWidget *line = new QWidget(&menu);
    line->setFixedHeight(9);
    QColor lineColor = menu.palette().color(QPalette::WindowText);
    lineColor.setAlpha(70);
    QFrame *rule = new QFrame(line);
    rule->setStyleSheet(QStringLiteral("background: %1;").arg(lineColor.name(QColor::HexArgb)));
    QHBoxLayout *lineLayout = new QHBoxLayout(line);
    lineLayout->setContentsMargins(10, 4, 10, 4);
    lineLayout->addWidget(rule);
    QWidgetAction *separator = new QWidgetAction(&menu);
    separator->setDefaultWidget(line);
    separator->setEnabled(false);
    menu.addAction(separator);
    menu.addAction(noIcon, "Quit", this, &MascotWindow::quitRequested)->setIconVisibleInMenu(true);

    QPoint pos = mapToGlobal(QPoint(spriteRect.right() + 1, spriteRect.top() + spriteRect.height()/3));
    QScreen *screen = QGuiApplication::screenAt(pos);
    QSize size = menu.sizeHint();
    if(screen != nullptr && pos.x() + size.width() > screen->availableGeometry().right())
        pos.setX(mapToGlobal(spriteRect.topLeft()).x() - size.width() - 1);
    if(screen != nullptr && pos.y() + size.height() > screen->availableGeometry().bottom())
        pos.setY(screen->availableGeometry().bottom() - size.height());
    menu.exec(pos);
    (void)event;
}


//Default: bottom right corner of the primary screen. A saved position off every screen falls back to it.
void MascotWindow::loadAnchor()
{
    QSettings settings;
    QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    QPoint defaultAnchor = available.bottomRight() - QPoint(110, 10);
    anchor = settings.value("mascotAnchor", defaultAnchor).toPoint();
    if(QGuiApplication::screenAt(anchor - QPoint(0, 10)) == nullptr)    anchor = defaultAnchor;
}


void MascotWindow::saveAnchor()
{
    QSettings settings;
    settings.setValue("mascotAnchor", anchor);
}
