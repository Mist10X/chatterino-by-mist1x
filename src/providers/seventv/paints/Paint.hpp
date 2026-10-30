#pragma once

#include "providers/seventv/paints/PaintDropShadow.hpp"

#include <QBrush>
#include <QFont>

#include <vector>

namespace chatterino {

class Paint
{
public:
    virtual QBrush asBrush(QColor userColor, QRectF drawingRect) const = 0;
    virtual const std::vector<PaintDropShadow> &getDropShadows() const = 0;
    virtual bool animated() const = 0;

    /// With `margin`, the picture has room around the text for the shadow
    /// (a glow cut at the edges shows as a box): it is `*margin` bigger on
    /// every side and is drawn that much up and to the left.
    QPixmap getPixmap(const QString &text, const QFont &font, QColor userColor,
                      QSizeF size, float scale, float dpr,
                      bool centerVertically = false,
                      qreal *margin = nullptr) const;

    Paint(QString id, QString name = {})
        : id(std::move(id))
        , name(std::move(name))
    {
    }
    virtual ~Paint() = default;

    Paint(const Paint &) = default;
    Paint(Paint &&) = delete;
    Paint &operator=(const Paint &) = default;
    Paint &operator=(Paint &&) = delete;

    QString id;
    QString name;

protected:
    static QColor overlayColors(QColor background, QColor foreground);
    static qreal offsetRepeatingStopPosition(qreal position,
                                             const QGradientStops &stops);
};

}  // namespace chatterino
