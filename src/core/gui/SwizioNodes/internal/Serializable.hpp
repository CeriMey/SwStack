#pragma once

/**
 * @file
 * @ingroup core_swizio_nodes
 * @brief Declares the minimal serialization interface used by the node-editor module.
 *
 * Types implementing `Serializable` can persist their state to and from `SwJsonObject`
 * instances. The interface stays intentionally small so node styles, models, or scene
 * helpers can opt into save/load support without inheriting a larger framework surface.
 */




#include "core/types/SwJsonObject.h"

namespace SwizioNodes {

class Serializable
{
public:
    /**
     * @brief Destroys the `Serializable` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~Serializable() = default;

    /**
     * @brief Returns the current save.
     * @return The current save.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwJsonObject save() const { return {}; }

    /**
     * @brief Performs the `load` operation on the associated resource.
     * @return The resulting load.
     */
    virtual void load(SwJsonObject const& /*p*/) {}
};

} // namespace SwizioNodes
