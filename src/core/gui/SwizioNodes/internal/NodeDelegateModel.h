#pragma once

#include <memory>

#include "core/object/SwObject.h"
#include "core/types/SwJsonObject.h"
#include "core/types/SwString.h"

#include "Definitions.hpp"
#include "Export.hpp"
#include "NodeData.hpp"
#include "NodeStyle.hpp"
#include "Serializable.hpp"
#include "StyleCollection.hpp"

class SwWidget;

namespace SwizioNodes {

/**
 * The class wraps Node-specific data operations and propagates it to
 * the nesting DataFlowGraphModel which is a subclass of
 * AbstractGraphModel.
 * This class is the same what has been called NodeDataModel before v3.
 */
class SWIZIO_NODES_PUBLIC NodeDelegateModel : public SwObject, public Serializable
{
    SW_OBJECT(NodeDelegateModel, SwObject)

public:
    explicit NodeDelegateModel(const SwString& context);

    ~NodeDelegateModel() override = default;

    /// It is possible to hide caption in GUI
    virtual bool captionVisible() const { return true; }

    /// Caption is used in GUI
    virtual SwString caption() const = 0;

    /// It is possible to hide port caption in GUI
    virtual bool portCaptionVisible(PortType, PortIndex) const { return false; }

    /// Port caption is used in GUI to label individual ports
    virtual SwString portCaption(PortType, PortIndex) const { return SwString(); }

    /// Name makes this model unique
    virtual SwString name() const = 0;

public:
    SwJsonObject save() const override;

    void load(SwJsonObject const&) override;

public:
    virtual unsigned int nPorts(PortType portType) const = 0;

    virtual NodeDataType dataType(PortType portType, PortIndex portIndex) const = 0;

    virtual bool dataPropagationOnConnection(PortIndex) { return true; }

public:
    virtual ConnectionPolicy portConnectionPolicy(PortType, PortIndex) const;

    NodeStyle const& nodeStyle() const;

    void setNodeStyle(NodeStyle const& style);

public:
    virtual void setInData(std::shared_ptr<NodeData>, PortIndex const) {}

    virtual std::shared_ptr<NodeData> outData(PortIndex const) { return nullptr; }

    /**
     * Lazy widget initialization: create the embedded widget inside this
     * function, not in the constructor, to avoid dangling pointers.
     */
    virtual SwWidget* embeddedWidget() { return nullptr; }

    virtual SwWidget* sideWidget() { return nullptr; }

    virtual bool resizable() const { return false; }

public slots:
    virtual void inputConnectionCreated(ConnectionId const&) {}
    virtual void inputConnectionDeleted(ConnectionId const&) {}
    virtual void outputConnectionCreated(ConnectionId const&) {}
    virtual void outputConnectionDeleted(ConnectionId const&) {}

signals:
    /// Triggers the updates in the nodes downstream.
    DECLARE_SIGNAL(dataUpdated, PortIndex);

    /// Triggers the propagation of the empty data downstream.
    DECLARE_SIGNAL(dataInvalidated, PortIndex);

    DECLARE_SIGNAL_VOID(computingStarted);
    DECLARE_SIGNAL_VOID(computingFinished);
    DECLARE_SIGNAL_VOID(embeddedWidgetSizeUpdated);

    /// Call this function before deleting the data associated with ports.
    DECLARE_SIGNAL(portsAboutToBeDeleted, PortType const, PortIndex const, PortIndex const);
    DECLARE_SIGNAL_VOID(portsDeleted);

    /// Call this function before inserting the data associated with ports.
    DECLARE_SIGNAL(portsAboutToBeInserted, PortType const, PortIndex const, PortIndex const);
    DECLARE_SIGNAL_VOID(portsInserted);

protected:
    SwString m_nodeContext;

private:
    NodeStyle m_nodeStyle;
};

} // namespace SwizioNodes
