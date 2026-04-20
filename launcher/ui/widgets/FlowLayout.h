// SPDX-License-Identifier: BSD-3-Clause
/*
 * FlowLayout — canonical Qt example layout that arranges items left-to-right,
 * wrapping to a new row when horizontal space runs out.
 *
 * Adapted from the Qt Widgets "Flow Layout Example" (Qt Company, BSD-3):
 *   https://doc.qt.io/qt-6/qtwidgets-layouts-flowlayout-example.html
 */

#pragma once

#include <QLayout>
#include <QRect>
#include <QStyle>

class FlowLayout : public QLayout {
   public:
    explicit FlowLayout(QWidget* parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QLayoutItem* takeAt(int index) override;

   private:
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> m_items;
    int m_hSpace;
    int m_vSpace;
};
