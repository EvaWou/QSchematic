#pragma once

#include <QFont>

#include "item.h"

namespace QSchematic {

    class Label : public Item
    {
        Q_OBJECT
        Q_DISABLE_COPY(Label)

        friend class CommandLabelRename;

    signals:
        void textChanged(const QString& newText);

    public:
        Label(int type = Item::LabelType, QGraphicsItem* parent = nullptr);
        virtual ~Label() override = default;

        virtual Gpds::Container toContainer() const override;
        virtual void fromContainer(const Gpds::Container& container) override;
        virtual std::unique_ptr<Item> deepCopy() const override;

        virtual QRectF boundingRect() const final;
        virtual QPainterPath shape() const final;

        void setText(const QString& text);
        QString text() const;
        void setFont(const QFont& font);
        QFont font() const;
        void setHasConnectionPoint(bool enabled);
        bool hasConnectionPoint() const;
        void setConnectionPoint(const QPointF& connectionPoint);    // Parent coordinates
        QRectF textRect() const;

    protected:
        void copyAttributes(Label& dest) const;
        void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    private:
        void calculateTextRect();

        QString _text;
        QFont _font;
        QRectF _textRect;
        bool _hasConnectionPoint;
        QPointF _connectionPoint;   // Parent coordinates
    };

}
