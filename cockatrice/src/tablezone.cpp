#include <QPainter>
#include <QSet>
#include <QGraphicsScene>
#include <cmath>
#ifdef _WIN32
#include "round.h"
#endif /* _WIN32 */
#include "tablezone.h"
#include "player.h"
#include "settingscache.h"
#include "arrowitem.h"
#include "carddragitem.h"
#include "carddatabase.h"
#include "carditem.h"

#include "pb/command_move_card.pb.h"
#include "pb/command_set_card_attr.pb.h"


const QColor TableZone::BACKGROUND_COLOR    = QColor(70, 50, 100);
const QColor TableZone::FADE_MASK           = QColor(0, 0, 0, 80);
const QColor TableZone::GRADIENT_COLOR      = QColor(255, 255, 255, 150);
const QColor TableZone::GRADIENT_COLORLESS  = QColor(255, 255, 255, 0);


TableZone::TableZone(Player *_p, QGraphicsItem *parent)
    : SelectZone(_p, "table", true, false, true, parent), active(false)
{
    // repaint the table if the background image changes
    connect(settingsCache, SIGNAL(tableBgPathChanged()), this, SLOT(updateBgPixmap()));

    // reorganize the cards if the vertical coordinates are changed
    connect(settingsCache, SIGNAL(invertVerticalCoordinateChanged()), this, SLOT(reorganizeCards()));
    
    // load in any current background pixel map
    updateBgPixmap();

    // calculate the width and height of the TableZone bounding rect
    height = 2 * BOX_LINE_WIDTH + 3 * (CARD_HEIGHT + 20) + 2 * PADDING_Y;
    width = MIN_WIDTH + 2 * MARGIN_X + 2 * BOX_LINE_WIDTH;
    currentMinimumWidth = MIN_WIDTH;

    setCacheMode(DeviceCoordinateCache);
#if QT_VERSION < 0x050000
    setAcceptsHoverEvents(true);
#else
    setAcceptHoverEvents(true);
#endif
}


void TableZone::updateBgPixmap()
{
    QString bgPath = settingsCache->getTableBgPath();
    if (!bgPath.isEmpty())
        backgroundPixelMap.load(bgPath);
    update();
}


QRectF TableZone::boundingRect() const
{
    return QRectF(0, 0, width, height);
}


bool TableZone::isInverted() const
{
    return ((player->getMirrored() && !settingsCache->getInvertVerticalCoordinate()) || (!player->getMirrored() && settingsCache->getInvertVerticalCoordinate()));
}


void TableZone::paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/)
{
    // if no custom background is provided then use the default color
    if (backgroundPixelMap.isNull())
        painter->fillRect(boundingRect(), BACKGROUND_COLOR);
    else
        painter->fillRect(boundingRect(), QBrush(backgroundPixelMap));

    if (active) {
        paintZoneOutline(painter);
    } else {
        // inactive player gets a darker table zone with a semi transparent black mask
        // this means if the user provides a custom background it will fade
        painter->fillRect(boundingRect(), FADE_MASK);
    }

    paintLandDivider(painter);
}


/**
   Render a soft outline around the edge of the TableZone.

   @param painter QPainter object
 */
void TableZone::paintZoneOutline(QPainter *painter) {
    QLinearGradient grad1(0, 0, 0, 1);
    grad1.setCoordinateMode(QGradient::ObjectBoundingMode);
    grad1.setColorAt(0, GRADIENT_COLOR);
    grad1.setColorAt(1, GRADIENT_COLORLESS);
    painter->fillRect(QRectF(0, 0, width, BOX_LINE_WIDTH), QBrush(grad1));

    grad1.setFinalStop(1, 0);
    painter->fillRect(QRectF(0, 0, BOX_LINE_WIDTH, height), QBrush(grad1));

    grad1.setStart(0, 1);
    grad1.setFinalStop(0, 0);
    painter->fillRect(QRectF(0, height - BOX_LINE_WIDTH, width, BOX_LINE_WIDTH), QBrush(grad1));

    grad1.setStart(1, 0);
    painter->fillRect(QRectF(width - BOX_LINE_WIDTH, 0, BOX_LINE_WIDTH, height), QBrush(grad1));
}


/**
   Render a division line for land placement

   @painter QPainter object
 */
void TableZone::paintLandDivider(QPainter *painter){
    painter->setPen(QColor(255, 255, 255, 40));
    qreal separatorY = 2 * (CARD_HEIGHT + 20 + PADDING_Y) + BOX_LINE_WIDTH - PADDING_Y / 2;
    if (isInverted())
        separatorY = height - separatorY;
    painter->drawLine(QPointF(0, separatorY), QPointF(width, separatorY));
}


void TableZone::addCardImpl(CardItem *card, int _x, int _y)
{
    cards.append(card);
    card->setGridPoint(QPoint(_x, _y));

    card->setParentItem(this);
    card->setVisible(true);
    card->update();
}


void TableZone::handleDropEvent(const QList<CardDragItem *> &dragItems, CardZone *startZone, const QPoint &dropPoint)
{
    handleDropEventByGrid(dragItems, startZone, mapToGrid(dropPoint));
}


void TableZone::handleDropEventByGrid(const QList<CardDragItem *> &dragItems, CardZone *startZone, const QPoint &gridPoint)
{
    Command_MoveCard cmd;
    cmd.set_start_player_id(startZone->getPlayer()->getId());
    cmd.set_start_zone(startZone->getName().toStdString());
    cmd.set_target_player_id(player->getId());
    cmd.set_target_zone(getName().toStdString());
    cmd.set_x(gridPoint.x());
    cmd.set_y(gridPoint.y());
    
    for (int i = 0; i < dragItems.size(); ++i) {
        CardToMove *ctm = cmd.mutable_cards_to_move()->add_card();
        ctm->set_card_id(dragItems[i]->getId());
        ctm->set_face_down(dragItems[i]->getFaceDown());
        ctm->set_pt(startZone->getName() == name ? std::string() : dragItems[i]->getItem()->getInfo()->getPowTough().toStdString());
    }
    
    startZone->getPlayer()->sendGameCommand(cmd);
}


void TableZone::reorganizeCards()
{
    QList<ArrowItem *> arrowsToUpdate;
    
    // Calculate table grid distortion so that the mapping functions work properly
    QMap<int, int> gridPointStackCount;
    for (int i = 0; i < cards.size(); ++i) {
        const QPoint &gridPoint = cards[i]->getGridPos();
        if (gridPoint.x() == -1)
            continue;
        
        const int key = gridPoint.x() / 3 + gridPoint.y() * 1000;
        gridPointStackCount.insert(key, gridPointStackCount.value(key, 0) + 1);
    }
    gridPointWidth.clear();
    for (int i = 0; i < cards.size(); ++i) {
        const QPoint &gridPoint = cards[i]->getGridPos();
        if (gridPoint.x() == -1)
            continue;
        
        const int key = gridPoint.x() / 3 + gridPoint.y() * 1000;
        const int stackCount = gridPointStackCount.value(key, 0);
        if (stackCount == 1)
            gridPointWidth.insert(key, CARD_WIDTH * (1 + cards[i]->getAttachedCards().size() / 3.0));
        else
            gridPointWidth.insert(key, CARD_WIDTH * (1 + (stackCount - 1) / 3.0));
    }
    
    for (int i = 0; i < cards.size(); ++i) {
        QPoint gridPoint = cards[i]->getGridPos();
        if (gridPoint.x() == -1)
            continue;
        
        QPointF mapPoint = mapFromGrid(gridPoint);
        qreal x = mapPoint.x();
        qreal y = mapPoint.y();
        
        int numberAttachedCards = cards[i]->getAttachedCards().size();
        qreal actualX = x + numberAttachedCards * CARD_WIDTH / 3.0;
        qreal actualY = y;
        if (numberAttachedCards)
            actualY += 15;
        
        cards[i]->setPos(actualX, actualY);
        cards[i]->setRealZValue((actualY + CARD_HEIGHT) * 100000 + (actualX + 1) * 100);
        
        QListIterator<CardItem *> attachedCardIterator(cards[i]->getAttachedCards());
        int j = 0;
        while (attachedCardIterator.hasNext()) {
            ++j;
            CardItem *attachedCard = attachedCardIterator.next();
            qreal childX = actualX - j * CARD_WIDTH / 3.0;
            qreal childY = y + 5;
            attachedCard->setPos(childX, childY);
            attachedCard->setRealZValue((childY + CARD_HEIGHT) * 100000 + (childX + 1) * 100);

            arrowsToUpdate.append(attachedCard->getArrowsFrom());
            arrowsToUpdate.append(attachedCard->getArrowsTo());
        }
        
        arrowsToUpdate.append(cards[i]->getArrowsFrom());
        arrowsToUpdate.append(cards[i]->getArrowsTo());
    }

    QSetIterator<ArrowItem *> arrowIterator(QSet<ArrowItem *>::fromList(arrowsToUpdate));
    while (arrowIterator.hasNext())
        arrowIterator.next()->updatePath();
    
    resizeToContents();
    update();
}


void TableZone::toggleTapped()
{
    QList<QGraphicsItem *> selectedItems = scene()->selectedItems();
    bool tapAll = false;
    for (int i = 0; i < selectedItems.size(); i++)
        if (!qgraphicsitem_cast<CardItem *>(selectedItems[i])->getTapped()) {
            tapAll = true;
            break;
        }
    QList< const ::google::protobuf::Message * > cmdList;
    for (int i = 0; i < selectedItems.size(); i++) {
        CardItem *temp = qgraphicsitem_cast<CardItem *>(selectedItems[i]);
        if (temp->getTapped() != tapAll) {
            Command_SetCardAttr *cmd = new Command_SetCardAttr;
            cmd->set_zone(name.toStdString());
            cmd->set_card_id(temp->getId());
            cmd->set_attribute(AttrTapped);
            cmd->set_attr_value(tapAll ? "1" : "0");
            cmdList.append(cmd);
        }
    }
    player->sendGameCommand(player->prepareGameCommand(cmdList));
}


CardItem *TableZone::takeCard(int position, int cardId, bool canResize)
{
    CardItem *result = CardZone::takeCard(position, cardId);
    if (canResize)
        resizeToContents();
    return result;
}


void TableZone::resizeToContents()
{
    int xMax = 0;
    for (int i = 0; i < cards.size(); ++i)
        if (cards[i]->pos().x() > xMax)
            xMax = (int) cards[i]->pos().x();
    xMax += 2 * CARD_WIDTH;
    if (xMax < MIN_WIDTH)
        xMax = MIN_WIDTH;
    currentMinimumWidth = xMax + 2 * MARGIN_X + 2 * BOX_LINE_WIDTH;
    if (currentMinimumWidth != width) {
        prepareGeometryChange();
        width = currentMinimumWidth;
        emit sizeChanged();
    }
}


CardItem *TableZone::getCardFromGrid(const QPoint &gridPoint) const
{
    for (int i = 0; i < cards.size(); i++)
        if (cards.at(i)->getGridPoint() == gridPoint)
            return cards.at(i);
    return 0;
}

CardItem *TableZone::getCardFromCoords(const QPointF &point) const
{
    QPoint gridPoint = mapToGrid(point);
    return getCardFromGrid(gridPoint);
}


QPointF TableZone::mapFromGrid(QPoint gridPoint) const
{
    qreal x, y;
    x = MARGIN_X + (gridPoint.x() % 3) * CARD_WIDTH / 3.0;
    for (int i = 0; i < gridPoint.x() / 3; ++i)
        x += gridPointWidth.value(gridPoint.y() * 1000 + i, CARD_WIDTH) + PADDING_X;
    
    if (isInverted())
        gridPoint.setY(2 - gridPoint.y());
    
    y = BOX_LINE_WIDTH + gridPoint.y() * (CARD_HEIGHT + PADDING_Y + 20) + (gridPoint.x() % 3) * 10;
/*    
    if (isInverted())
        y = height - CARD_HEIGHT - y;
*/    
    return QPointF(x, y);
}


QPoint TableZone::mapToGrid(const QPointF &mapPoint) const
{
    qreal x = mapPoint.x() - MARGIN_X;
    qreal y = mapPoint.y();
/*    if (isInverted())
        y = height - y;
*/    y -= BOX_LINE_WIDTH;
    
    if (x < 0)
        x = 0;
    else if (x > width - CARD_WIDTH - MARGIN_X)
        x = width - CARD_WIDTH - MARGIN_X;
    if (y < 0)
        y = 0;
    else if (y > height - CARD_HEIGHT)
        y = height - CARD_HEIGHT;
    
    int resultY = round(y / (CARD_HEIGHT + PADDING_Y + 20));
    if (isInverted())
        resultY = 2 - resultY;

    int baseX = -1;
    qreal oldTempX = 0, tempX = 0;
    do {
        ++baseX;
        oldTempX = tempX;
        tempX += gridPointWidth.value(resultY * 1000 + baseX, CARD_WIDTH) + PADDING_X;
    } while (tempX < x + 1);
    
    qreal xdiff = x - oldTempX;
    int resultX = baseX * 3 + qMin((int) floor(xdiff * 3 / CARD_WIDTH), 2);
    return QPoint(resultX, resultY);
}


QPointF TableZone::closestGridPoint(const QPointF &point)
{
    QPoint gridPoint = mapToGrid(point + QPoint(1, 1));
    gridPoint.setX((gridPoint.x() / 3) * 3);
    if (getCardFromGrid(gridPoint))
        gridPoint.setX(gridPoint.x() + 1);
    if (getCardFromGrid(gridPoint))
        gridPoint.setX(gridPoint.x() + 1);
    return mapFromGrid(gridPoint);
}
