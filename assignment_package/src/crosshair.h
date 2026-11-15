#pragma once

#include <QWidget>
#include <QPainter>
#include <QRectF>

class Crosshair : public QWidget
{
public:
    explicit Crosshair(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);

        const qreal cx = width() / 2.0;
        const qreal cy = height() / 2.0;

        // currently a black outline and a white cross
        painter.fillRect(
            QRectF(cx - 9, cy - 2, 18, 4), Qt::black);
        painter.fillRect(
            QRectF(cx - 2, cy - 9, 4, 18), Qt::black);

        painter.fillRect(
            QRectF(cx - 8, cy - 1, 16, 2), Qt::white);
        painter.fillRect(
            QRectF(cx - 1, cy - 8, 2, 16), Qt::white);
    }
};