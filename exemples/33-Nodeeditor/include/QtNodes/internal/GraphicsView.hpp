#pragma once

#include <QtWidgets/QGraphicsView>
#include <QDebug>
#include "Export.hpp"
#include <QDragMoveEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QGraphicsTextItem>

namespace QtNodes {

class BasicGraphicsScene;

/**
 * @brief A central view able to render objects from `BasicGraphicsScene`.
 */
class NODE_EDITOR_PUBLIC GraphicsView : public QGraphicsView
{
    Q_OBJECT
public:
    struct ScaleRange
    {
        double minimum = 0;
        double maximum = 0;
    };

public:
    GraphicsView(QWidget *parent = Q_NULLPTR);
    GraphicsView(BasicGraphicsScene *scene, QWidget *parent = Q_NULLPTR);

    GraphicsView(const GraphicsView &) = delete;
    GraphicsView operator=(const GraphicsView &) = delete;

    QAction *clearSelectionAction() const;

    QAction *deleteSelectionAction() const;

    void setScene(BasicGraphicsScene *scene);

    void centerScene();

    /// @brief max=0/min=0 indicates infinite zoom in/out
    void setScaleRange(double minimum = 0, double maximum = 0);

    void setScaleRange(ScaleRange range);

    double getScale() const;

public Q_SLOTS:
    void scaleUp();

    void scaleDown();

    void setupScale(double scale);

    void onDeleteSelectedObjects();

    void onDuplicateSelectedObjects();

    void onCopySelectedObjects();

    void onCutSelectedObjects();

    void onPasteObjects();

Q_SIGNALS:
    void scaleChanged(double scale);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;

    void keyReleaseEvent(QKeyEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void drawBackground(QPainter *painter, const QRectF &r) override;

    void showEvent(QShowEvent *event) override;

    // void dragEnterEvent(QDragEnterEvent *event) override {
    //     if (event->mimeData()->hasFormat("text/plain")) {
    //         event->acceptProposedAction();
    //     }
    // }

    // void dragMoveEvent(QDragMoveEvent *event) override {
    //     if (event->mimeData()->hasFormat("text/plain")) {
    //         event->acceptProposedAction();
    //     }
    // }

    // void dropEvent(QDropEvent *event) override {
    //     if (event->mimeData()->hasText()) {
    //         QGraphicsScene *scene = this->scene();
    //         QString text = event->mimeData()->text();
    //         auto textItem = scene->addText(text);
    //         textItem->setPos(mapToScene(event->pos()));
    //         event->acceptProposedAction();
    //     }
    // }
protected:
    BasicGraphicsScene *nodeScene();

    /// Computes scene position for pasting the copied/duplicated node groups.
    QPointF scenePastePosition();

private:
    QAction *_clearSelectionAction = nullptr;
    QAction *_deleteSelectionAction = nullptr;
    QAction *_duplicateSelectionAction = nullptr;
    QAction *_copySelectionAction = nullptr;
    QAction *_cutSelectionAction = nullptr;
    QAction *_pasteAction = nullptr;
    QAction *_undoAction = nullptr;
    QAction *_redoAction = nullptr;

    QPointF _clickPos;
    ScaleRange _scaleRange;
};
} // namespace QtNodes
