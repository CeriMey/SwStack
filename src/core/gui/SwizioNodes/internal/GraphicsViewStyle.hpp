#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/GraphicsViewStyle.hpp
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by GraphicsViewStyle in the CoreSw node-editor
 * layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the graphics view style interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * View-oriented declarations here mainly describe how underlying state is projected into a visual
 * or interactive surface, including how refresh, selection, or presentation concerns are exposed
 * at the API boundary.
 *
 * Most declarations here are extension points or internal contracts that coordinate graph
 * editing, visualization, and interaction.
 *
 */


#include "Export.hpp"
#include "Style.hpp"

#include "ColorUtils.hpp"

#include "core/types/Sw.h"
#include "core/types/SwString.h"

namespace SwizioNodes {

class SWIZIO_NODES_PUBLIC GraphicsViewStyle : public Style
{
public:
    /**
     * @brief Constructs a `GraphicsViewStyle` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    GraphicsViewStyle()
    {
        // Defaults mirror `exemples/33-Nodeeditor/resources/DefaultStyle.json`.
        BackgroundColor = SwColor{53, 53, 53};
        FineGridColor = SwColor{60, 60, 60};
        CoarseGridColor = SwColor{25, 25, 25};
    }

    /**
     * @brief Constructs a `GraphicsViewStyle` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit GraphicsViewStyle(SwString jsonText) { loadJsonText(std::move(jsonText)); }

    /**
     * @brief Destroys the `GraphicsViewStyle` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~GraphicsViewStyle() override = default;

public:
    /**
     * @brief Sets the style.
     * @param jsonText Value passed to the method.
     * @return The requested style.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    static void setStyle(SwString jsonText);

private:
    void loadJson(SwJsonObject const& json) override;

    SwJsonObject toJson() const override;

public:
    SwColor BackgroundColor;
    SwColor FineGridColor;
    SwColor CoarseGridColor;
};

inline void GraphicsViewStyle::loadJson(SwJsonObject const& json)
{
    SwJsonObject obj = json.contains("GraphicsViewStyle") && json["GraphicsViewStyle"].isObject()
                           ? json["GraphicsViewStyle"].toObject()
                           : json;

    if (obj.contains("BackgroundColor")) {
        BackgroundColor = parseColorValue_(obj["BackgroundColor"], BackgroundColor);
    }
    if (obj.contains("FineGridColor")) {
        FineGridColor = parseColorValue_(obj["FineGridColor"], FineGridColor);
    }
    if (obj.contains("CoarseGridColor")) {
        CoarseGridColor = parseColorValue_(obj["CoarseGridColor"], CoarseGridColor);
    }
}

inline SwJsonObject GraphicsViewStyle::toJson() const
{
    SwJsonObject obj;
    obj["BackgroundColor"] = toHex_(BackgroundColor);
    obj["FineGridColor"] = toHex_(FineGridColor);
    obj["CoarseGridColor"] = toHex_(CoarseGridColor);

    SwJsonObject root;
    root["GraphicsViewStyle"] = obj;
    return root;
}

} // namespace SwizioNodes
