#include <QPen>
#include <QBrush>
#include <QPainter>
#include <QJsonArray>
#include <QMap>
#include <QGraphicsSceneHoverEvent>
#include <QApplication>
#include <QMetaEnum>
#include <QVector2D>
#include "wire.h"

const qreal BOUNDING_RECT_PADDING = 6.0;
const qreal HANDLE_SIZE = 3.0;
const qreal WIRE_SHAPE_PADDING = 10;
const QColor COLOR_WIRE                     = QColor("#000000");
const QColor COLOR_WIRE_HIGHLIGHTED         = QColor("#dc2479");
const QColor COLOR_WIRE_SELECTED            = QColor("#0f16af");
const QColor COLOR_BUS                      = QColor("#0f16af");
const QColor COLOR_BUS_HIGHLIGHTED          = QColor("#dc2479");
const QColor COLOR_BUS_SELECTED             = QColor("#1ca949");

using namespace QSchematic;

class PointWithIndex {
public:
    PointWithIndex(int index, const QPoint& point) : index(index), point(point) {}
    int index;
    QPoint point;

    bool operator<(const PointWithIndex& other) const {
        return index < other.index;
    }
};

Wire::Wire(QGraphicsItem* parent) :
    Item(Item::WireType, parent)
{
    _pointToMoveIndex = -1;
    _lineSegmentToMoveIndex = -1;

    // Lines should always be the lowest item in Z-Order
    setZValue(-10);

    // ALWAYS snap to grid
    setSnapToGrid(true);
}

void Wire::update()
{
    calculateBoundingRect();

    Item::update();
}

QRectF Wire::boundingRect() const
{
    return _rect.adjusted(-BOUNDING_RECT_PADDING, -BOUNDING_RECT_PADDING, BOUNDING_RECT_PADDING, BOUNDING_RECT_PADDING);
}

QPainterPath Wire::shape() const
{
    QPainterPath basePath;
    basePath.addPolygon(QPolygon(scenePointsRelative()));

    QPainterPathStroker str;
    str.setCapStyle(Qt::FlatCap);
    str.setJoinStyle(Qt::MiterJoin);
    str.setWidth(WIRE_SHAPE_PADDING);

    QPainterPath resultPath = str.createStroke(basePath).simplified();

    return resultPath;
}

QVector<WirePoint> Wire::sceneWirePointsRelative() const
{
    QVector<WirePoint> points;

    for (const WirePoint& point : _points) {
        WirePoint&& tmp = WirePoint(_settings.toScenePoint(point.toPoint()));
        tmp.setIsJunction(point.isJunction());
        points.append(tmp);
    }

    return points;
}

QVector<QPoint> Wire::scenePointsRelative() const
{
    QVector<QPoint> points;

    for (const WirePoint& point : _points) {
        points << _settings.toScenePoint(point.toPoint());
    }

    return points;
}

QVector<QPoint> Wire::scenePointsAbsolute() const
{
    QVector<QPoint> points;

    for (const WirePoint& point : _points) {
        points << _settings.toScenePoint(point.toPoint() + gridPoint());
    }

    return points;
}

void Wire::calculateBoundingRect()
{
    // Find top-left most point
    QPointF topLeft(INT_MAX, INT_MAX);
    for (auto& point : _points) {
        if (point.x() < topLeft.x())
            topLeft.setX(point.x());
        if (point.y() < topLeft.y())
            topLeft.setY(point.y());
    }

    // Find bottom-right most point
    QPointF bottomRight(INT_MIN, INT_MIN);
    for (auto& point : _points) {
        if (point.x() > bottomRight.x())
            bottomRight.setX(point.x());
        if (point.y() > bottomRight.y())
            bottomRight.setY(point.y());
    }

    // Create the rectangle
    _rect = QRectF(topLeft * _settings.gridSize, bottomRight * _settings.gridSize);
}

void Wire::prependPoint(const QPoint& point)
{
    prepareGeometryChange();
    _points.prepend(WirePoint(point - gridPoint()));
    calculateBoundingRect();

    update();

    emit pointMoved(*this, _points.first());
}

void Wire::appendPoint(const QPoint& point)
{
    prepareGeometryChange();
    _points.append(WirePoint(point - gridPoint()));
    calculateBoundingRect();

    update();

    emit pointMoved(*this, _points.last());
}

void Wire::insertPoint(int index, const QPoint& point)
{
    // Boundary check
    if (index < 0 || index >= _points.count()) {
        return;
    }

    prepareGeometryChange();
    _points.insert(index, WirePoint(point - gridPoint()));
    calculateBoundingRect();

    update();

    emit pointMoved(*this, _points[index]);
}

void Wire::removeFirstPoint()
{
    if (_points.count() <= 0) {
        return;
    }
    prepareGeometryChange();
    _points.removeFirst();
    calculateBoundingRect();

    update();
}

void Wire::removeLastPoint()
{
    if (_points.count() <= 0) {
        return;
    }

    prepareGeometryChange();
    _points.removeLast();
    calculateBoundingRect();

    update();
}

void Wire::removePoint(const QPoint& point)
{
    prepareGeometryChange();
    _points.removeAll(WirePoint(point - gridPoint()));
    calculateBoundingRect();

    update();
}

int Wire::removeDuplicatePoints()
{
    int removedCount = 0;
    bool allUnique = false;

    do {
        // Go through all points and remove duplicates
        for (const WirePoint& point : _points) {
            int count = _points.count(point);
            if (count > 1) {
                for (int i = 0; i < count-1; i++) {
                    _points.removeOne(point);
                    removedCount++;
                }
            }
        }

        // Check if uniques only now. Otherwise keep going.
        allUnique = true;
        for (const WirePoint& point  : _points) {
            if (_points.count(point) > 1) {
                allUnique = false;
            }
        }


    } while (!allUnique);

    return removedCount;
}

int Wire::removeObsoletePoints()
{
   /*
    * convert any 2 neighbouring points to a translation vector (i.e. subtract the second from the first)
    * then for each 2 consecutive translations take the scalar product.
    * if the scalar product is 0 then they're orthogonal.
    * if they're on exactly the same line p.q = |p|*|q|
    * this way we can easily deduce if a given point should be dropped.
    * if we drop it, the recreate the current translation vector (i.e. merge the two segments by choosing
    * the first and third point) and continue on with the next one.
    */

    // Don't do anything if there is not at least one line segment
    if (_points.count() < 3) {
        return 0;
    }

    QList<WirePoint> pointsToRemove;

    for (int i = 2; i < _points.count(); i++) {
        QPoint p1 = _points.at(i - 2).toPoint();
        QPoint p2 = _points.at(i - 1).toPoint();
        QPoint p3 = _points.at(i).toPoint();

        QVector2D v1(p2 - p1);
        QVector2D v2(p3 - p2);

        float dotProduct = QVector2D::dotProduct(v1, v2);
        float absProduct =  v1.length() * v2.length();

        if (qFuzzyCompare(dotProduct, absProduct)) {
            pointsToRemove.append(_points[i-1]);
        }
    }

    for (const WirePoint& point : pointsToRemove) {
        removePoint(point.toPoint());
    }

    return pointsToRemove.count();
}

void Wire::movePointBy(int index, const QVector2D& moveBy)
{
    if (index < 0 or index > _points.count()-1) {
        return;
    }

    prepareGeometryChange();
    _points[index] = WirePoint(_points.at(index).toPoint() + moveBy.toPoint());
    calculateBoundingRect();
    update();

    emit pointMoved(*this, _points[index]);
}

void Wire::movePointTo(int index, const QPoint& moveTo)
{
    if (index < 0 or index > _points.count()-1) {
        return;
    }

    prepareGeometryChange();
    _points[index] = WirePoint(moveTo - gridPoint());
    calculateBoundingRect();    
    update();

    emit pointMoved(*this, _points[index]);
}

void Wire::moveLineSegmentBy(int index, const QVector2D& moveBy)
{
    if (index < 0 or index > _points.count()-1) {
        return;
    }

    // Move the line segment
    movePointBy(index, moveBy);
    movePointBy(index+1, moveBy);
}

void Wire::setPointIsJunction(int index, bool isJunction)
{
    if (index < 0 or index > _points.count()-1) {
        return;
    }

    _points[index].setIsJunction(isJunction);

    update();
}

bool Wire::pointIsOnWire(const QPoint& point) const
{
    for (const Line& lineSegment : lineSegments()) {
        if (lineSegment.containsPoint(point, 0)) {
            return true;
        }
    }

    return false;
}

QVector<QPoint> Wire::points() const
{
    QVector<QPoint> list;

    for (const WirePoint& wirePoint : _points) {
        list << gridPoint() + wirePoint.toPoint();
    }

    return list;
}

QList<Line> Wire::lineSegments() const
{
    // A line segment requires at least two points... duuuh
    if (_points.count() < 2) {
        return QList<Line>();
    }

    QList<Line> ret;
    for (int i = 0; i < _points.count()-1; i++) {
        ret.append(Line(gridPoint() + _points.at(i).toPoint(), gridPoint() + _points.at(i+1).toPoint()));
    }

    return ret;
}

void Wire::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    _prevMousePos = _settings.toGridPoint(event->scenePos());

    // Check wheter we clicked on a handle
    if (isSelected()) {
        // Check whether we clicked on a handle
        QVector<QPoint> points(scenePointsAbsolute());
        for (int i = 0; i < points.count(); i++) {
            QRectF handleRect(points.at(i).x() - HANDLE_SIZE, points.at(i).y() - HANDLE_SIZE, 2*HANDLE_SIZE, 2*HANDLE_SIZE);

            if (handleRect.contains(event->scenePos())) {
                _pointToMoveIndex = i;
                return;
            }

            _pointToMoveIndex = -1;
        }

        // Check whether we clicked on a line segment
        QList<Line> lines = lineSegments();
        for (int i = 0; i < lines.count(); i++) {
            const Line& line = lines.at(i);
            if (line.containsPoint(_settings.toGridPoint(event->scenePos()), 1)) {
                _lineSegmentToMoveIndex = i;
                return;
            }

            _lineSegmentToMoveIndex = -1;
        }

    } else {
        Item::mousePressEvent(event);
    }
}

void Wire::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    Item::mouseReleaseEvent(event);

    _pointToMoveIndex = -1;
    _lineSegmentToMoveIndex = -1;
    _prevMousePos = _settings.toGridPoint(event->scenePos());
}

void Wire::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    QPoint curPos = _settings.toGridPoint(event->scenePos());
    bool ctrlPressed = QApplication::keyboardModifiers() & Qt::ControlModifier;

    if (_pointToMoveIndex > -1) {

        event->accept();
        movePointTo(_pointToMoveIndex, curPos);

    } else if (_lineSegmentToMoveIndex > -1){

        event->accept();
        Line line = lineSegments().at(_lineSegmentToMoveIndex);
        QVector2D moveLineBy(0, 0);
        if (line.isHorizontal()) {
            moveLineBy = QVector2D(0, curPos.y() - _prevMousePos.y());
        } else if (line.isVertical()) {
            moveLineBy = QVector2D(curPos.x() - _prevMousePos.x(), 0);
        } else if (ctrlPressed){
            moveLineBy = QVector2D(curPos - _prevMousePos);
        }
        moveLineSegmentBy(_lineSegmentToMoveIndex, moveLineBy);

    } else {

        Item::mouseMoveEvent(event);
    }

    _prevMousePos = curPos;
}

void Wire::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    Item::hoverEnterEvent(event);
}

void Wire::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    Item::hoverLeaveEvent(event);

    unsetCursor();
}

void Wire::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    Item::hoverMoveEvent(event);

    // Only if wire is selected
    if (!isSelected()) {
        return;
    }

    // Check whether we hover over a point handle
    QVector<QPoint> points(scenePointsAbsolute());
    for (int i = 0; i < points.count(); i++) {
        QRectF handleRect(points.at(i).x() - HANDLE_SIZE, points.at(i).y() - HANDLE_SIZE, 2*HANDLE_SIZE, 2*HANDLE_SIZE);

        if (handleRect.contains(event->scenePos())) {
            setCursor(Qt::SizeAllCursor);
            return;
        }
    }

    // Check whether we hover over a line segment
    bool ctrlPressed = QApplication::keyboardModifiers() & Qt::ControlModifier;
    QList<Line> lines = lineSegments();
    for (int i = 0; i < lines.count(); i++) {
        const Line& line = lines.at(i);
        if (line.containsPoint(_settings.toGridPoint(event->scenePos()), 1)) {
            if (line.isHorizontal()) {
                setCursor(Qt::SizeVerCursor);
            } else if (line.isVertical()) {
                setCursor(Qt::SizeHorCursor);
            } else if (ctrlPressed) {
                setCursor(Qt::SizeAllCursor);
            }
            return;
        }
    }

    unsetCursor();
}

void Wire::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    // ToDo: Merge the swich() statements into one
    Q_UNUSED(option);
    Q_UNUSED(widget);

    QPen penLine;
    penLine.setStyle(Qt::SolidLine);
    penLine.setCapStyle(Qt::RoundCap);
    QColor penColor;
    if (isSelected()) {
        penColor = COLOR_WIRE_SELECTED;
    } else if (isHighlighted()) {
        penColor = COLOR_WIRE_HIGHLIGHTED;
    } else {
        penColor = COLOR_WIRE;
    }
    penLine.setWidth(1);
    penLine.setColor(penColor);

    QBrush brushLine;
    brushLine.setStyle(Qt::NoBrush);

    QPen penJunction;
    penJunction.setStyle(Qt::NoPen);

    QBrush brushJunction;
    brushJunction.setStyle(Qt::SolidPattern);
    brushJunction.setColor(isHighlighted() ? COLOR_WIRE_HIGHLIGHTED : COLOR_WIRE);

    QPen penHandle;
    penHandle.setColor(Qt::black);
    penHandle.setStyle(Qt::SolidLine);

    QBrush brushHandle;
    brushHandle.setColor(Qt::black);
    brushHandle.setStyle(Qt::SolidPattern);

    // Draw the actual line
    painter->setPen(penLine);
    painter->setBrush(brushLine);
    QVector<QPoint> points = scenePointsRelative();
    painter->drawPolyline(points.constData(), points.count());

    // Draw the junction poins
    int junctionRadius = 4;
    for (const WirePoint& wirePoint : sceneWirePointsRelative()) {
        if (wirePoint.isJunction()) {
            painter->setPen(penJunction);
            painter->setBrush(brushJunction);
            painter->drawEllipse(wirePoint.toPoint(), junctionRadius, junctionRadius);
        }
    }

    // Draw the handles (if selected)
    if (isSelected()) {
        painter->setPen(penHandle);
        painter->setBrush(brushHandle);
        for (const QPoint& point : points) {
            QRectF handleRect(point.x() - HANDLE_SIZE, point.y() - HANDLE_SIZE, 2*HANDLE_SIZE, 2*HANDLE_SIZE);
            painter->drawRect(handleRect);
        }
    }

    // Draw debugging stuff
    if (_settings.debug) {
        painter->setPen(Qt::red);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect());

        painter->setPen(Qt::blue);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(shape());
    }
}