#pragma once

#include "BasicGraphicsScene.hpp"
#include "DataFlowGraphModel.hpp"
#include "Export.hpp"
#include <QGraphicsSceneDragDropEvent>
#include <QMimeData>
#include <QGraphicsTextItem>
#include <QString>

#include <QDebug>
namespace QtNodes {

/// @brief An advanced scene working with data-propagating graphs.
/**
 * The class represents a scene that existed in v2.x but built wit the
 * new model-view approach in mind.
 */
class NODE_EDITOR_PUBLIC DataFlowGraphicsScene : public BasicGraphicsScene
{
    Q_OBJECT
public:
    DataFlowGraphicsScene(DataFlowGraphModel &graphModel, QObject *parent = nullptr);
    DataFlowGraphicsScene(std::shared_ptr<NodeDelegateModelRegistry> registry, QObject *parent = nullptr);

    ~DataFlowGraphicsScene() = default;

    void fromJson(QJsonObject sceneObject);
    QJsonObject toJson() const;
    bool isDurty();
    void setAsClean();

public:
    std::vector<NodeId> selectedNodes() const;

public:
    QMenu *createSceneMenu(QPointF const scenePos) override;
    void dropItem(QPointF const scenePos, QString itemName);


protected:
    void dragEnterEvent(QGraphicsSceneDragDropEvent *event) override {
        if (!this->views().isEmpty() && event->mimeData()->hasFormat("text/plain")) {
            event->acceptProposedAction();
        }
    }

    void dragMoveEvent(QGraphicsSceneDragDropEvent *event) override {
        if (!this->views().isEmpty() && event->mimeData()->hasFormat("text/plain")) {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QGraphicsSceneDragDropEvent *event) override {
        if (!this->views().isEmpty() && event->mimeData()->hasText()) {
            QString text = event->mimeData()->text();
            dropItem(event->scenePos(), text);
            event->acceptProposedAction();
        }
    }



public Q_SLOTS:
    void save() const;
    void load();

Q_SIGNALS:
    void sceneLoaded();
    void nodeSelected(NodeId const nodeId);

private:
    std::unique_ptr<DataFlowGraphModel> _graphModelOwned;
    DataFlowGraphModel &_graphModel;
};

} // namespace QtNodes
