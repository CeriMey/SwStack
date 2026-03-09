#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/NodeDelegateModel.h
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by NodeDelegateModel in the CoreSw node-editor
 * layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the node delegate model interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * Model-oriented declarations here define the data contract consumed by views, delegates, or
 * algorithms, with an emphasis on stable roles, ownership, and update flow rather than on
 * presentation details.
 *
 * Most declarations here are extension points or internal contracts that coordinate graph
 * editing, visualization, and interaction.
 *
 */


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
    /**
     * @brief Constructs a `NodeDelegateModel` instance.
     * @param context Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit NodeDelegateModel(const SwString& context);

    /**
     * @brief Destroys the `NodeDelegateModel` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
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
    /**
     * @brief Returns the current save.
     * @return The current save.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwJsonObject save() const override;

    /**
     * @brief Performs the `load` operation on the associated resource.
     */
    void load(SwJsonObject const&) override;

public:
    /**
     * @brief Performs the `nPorts` operation.
     * @param portType Value passed to the method.
     * @return The requested n Ports.
     */
    virtual unsigned int nPorts(PortType portType) const = 0;

    /**
     * @brief Performs the `dataType` operation.
     * @param portType Value passed to the method.
     * @param portIndex Value passed to the method.
     * @return The requested data Type.
     */
    virtual NodeDataType dataType(PortType portType, PortIndex portIndex) const = 0;

    /**
     * @brief Performs the `dataPropagationOnConnection` operation.
     * @param PortIndex Value passed to the method.
     * @return The requested data Propagation On Connection.
     */
    virtual bool dataPropagationOnConnection(PortIndex) { return true; }

public:
    /**
     * @brief Performs the `portConnectionPolicy` operation.
     * @param PortType Value passed to the method.
     * @param PortIndex Value passed to the method.
     * @return The requested port Connection Policy.
     */
    virtual ConnectionPolicy portConnectionPolicy(PortType, PortIndex) const;

    /**
     * @brief Returns the current node Style.
     * @return The current node Style.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    NodeStyle const& nodeStyle() const;

    /**
     * @brief Sets the node Style.
     * @param style Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setNodeStyle(NodeStyle const& style);

public:
    /**
     * @brief Sets the in Data.
     * @return The requested in Data.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    virtual void setInData(std::shared_ptr<NodeData>, PortIndex const) {}

    /**
     * @brief Performs the `outData` operation.
     * @return The requested out Data.
     */
    virtual std::shared_ptr<NodeData> outData(PortIndex const) { return nullptr; }

    /**
     * Lazy widget initialization: create the embedded widget inside this
     * function, not in the constructor, to avoid dangling pointers.
     */
    virtual SwWidget* embeddedWidget() { return nullptr; }

    /**
     * @brief Returns the current side Widget.
     * @return The current side Widget.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwWidget* sideWidget() { return nullptr; }

    /**
     * @brief Returns the current resizable.
     * @return The current resizable.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual bool resizable() const { return false; }

public slots:
    /**
     * @brief Performs the `inputConnectionCreated` operation.
     * @return The requested input Connection Created.
     */
    virtual void inputConnectionCreated(ConnectionId const&) {}
    /**
     * @brief Performs the `inputConnectionDeleted` operation.
     * @return The requested input Connection Deleted.
     */
    virtual void inputConnectionDeleted(ConnectionId const&) {}
    /**
     * @brief Performs the `outputConnectionCreated` operation.
     * @return The requested output Connection Created.
     */
    virtual void outputConnectionCreated(ConnectionId const&) {}
    /**
     * @brief Performs the `outputConnectionDeleted` operation.
     * @return The requested output Connection Deleted.
     */
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
