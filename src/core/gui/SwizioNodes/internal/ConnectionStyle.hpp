#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/ConnectionStyle.hpp
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by ConnectionStyle in the CoreSw node-editor
 * layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the connection style interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * Style-oriented declarations here capture reusable visual parameters so rendering code can stay
 * deterministic while still allowing higher-level customization.
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

class SWIZIO_NODES_PUBLIC ConnectionStyle : public Style
{
public:
    /**
     * @brief Constructs a `ConnectionStyle` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    ConnectionStyle();

    /**
     * @brief Constructs a `ConnectionStyle` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit ConnectionStyle(SwString jsonText) { loadJsonText(std::move(jsonText)); }

    /**
     * @brief Destroys the `ConnectionStyle` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~ConnectionStyle() override = default;

public:
    /**
     * @brief Sets the connection Style.
     * @param jsonText Value passed to the method.
     * @return The requested connection Style.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    static void setConnectionStyle(SwString jsonText);

public:
    /**
     * @brief Performs the `loadJson` operation on the associated resource.
     * @param json Value passed to the method.
     */
    void loadJson(SwJsonObject const& json) override;

    /**
     * @brief Returns the current to Json.
     * @return The current to Json.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwJsonObject toJson() const override;

public:
    /**
     * @brief Returns the current construction Color.
     * @return The current construction Color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor constructionColor() const { return ConstructionColor; }
    /**
     * @brief Returns the current normal Color.
     * @return The current normal Color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor normalColor() const { return NormalColor; }
    /**
     * @brief Performs the `normalColor` operation.
     * @param SwString Value passed to the method.
     * @return The requested normal Color.
     */
    SwColor normalColor(SwString /*typeId*/) const { return NormalColor; }
    /**
     * @brief Returns the current selected Color.
     * @return The current selected Color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor selectedColor() const { return SelectedColor; }
    /**
     * @brief Returns the current selected Halo Color.
     * @return The current selected Halo Color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor selectedHaloColor() const { return SelectedHaloColor; }
    /**
     * @brief Returns the current hovered Color.
     * @return The current hovered Color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor hoveredColor() const { return HoveredColor; }

    /**
     * @brief Returns the current line Width.
     * @return The current line Width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    float lineWidth() const { return LineWidth; }
    /**
     * @brief Returns the current construction Line Width.
     * @return The current construction Line Width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    float constructionLineWidth() const { return ConstructionLineWidth; }
    /**
     * @brief Returns the current point Diameter.
     * @return The current point Diameter.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    float pointDiameter() const { return PointDiameter; }

    /**
     * @brief Returns the current use Data Defined Colors.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool useDataDefinedColors() const { return UseDataDefinedColors; }

private:
    SwColor ConstructionColor;
    SwColor NormalColor;
    SwColor SelectedColor;
    SwColor SelectedHaloColor;
    SwColor HoveredColor;

    float LineWidth;
    float ConstructionLineWidth;
    float PointDiameter;

    bool UseDataDefinedColors;
};

inline ConnectionStyle::ConnectionStyle()
{
    // Defaults mirror `exemples/33-Nodeeditor/resources/DefaultStyle.json`.
    ConstructionColor = SwColor{128, 128, 128};     // "gray"
    NormalColor = SwColor{0, 139, 139};            // "darkcyan"
    SelectedColor = SwColor{100, 100, 100};
    SelectedHaloColor = SwColor{255, 165, 0};      // "orange"
    HoveredColor = SwColor{224, 255, 255};         // "lightcyan"

    LineWidth = 3.0f;
    ConstructionLineWidth = 2.0f;
    PointDiameter = 10.0f;

    UseDataDefinedColors = false;
}

inline void ConnectionStyle::loadJson(SwJsonObject const& json)
{
    SwJsonObject obj = json.contains("ConnectionStyle") && json["ConnectionStyle"].isObject()
                           ? json["ConnectionStyle"].toObject()
                           : json;

    auto readColor = [&](const char* key, SwColor& target) {
        if (!obj.contains(key)) {
            return;
        }
        target = parseColorValue_(obj[key], target);
    };
    auto readFloat = [&](const char* key, float& target) {
        if (!obj.contains(key)) {
            return;
        }
        target = static_cast<float>(obj[key].toDouble());
    };

    readColor("ConstructionColor", ConstructionColor);
    readColor("NormalColor", NormalColor);
    readColor("SelectedColor", SelectedColor);
    readColor("SelectedHaloColor", SelectedHaloColor);
    readColor("HoveredColor", HoveredColor);

    readFloat("LineWidth", LineWidth);
    readFloat("ConstructionLineWidth", ConstructionLineWidth);
    readFloat("PointDiameter", PointDiameter);

    if (obj.contains("UseDataDefinedColors")) {
        UseDataDefinedColors = obj["UseDataDefinedColors"].toBool();
    }
}

inline SwJsonObject ConnectionStyle::toJson() const
{
    SwJsonObject obj;
    obj["ConstructionColor"] = toHex_(ConstructionColor);
    obj["NormalColor"] = toHex_(NormalColor);
    obj["SelectedColor"] = toHex_(SelectedColor);
    obj["SelectedHaloColor"] = toHex_(SelectedHaloColor);
    obj["HoveredColor"] = toHex_(HoveredColor);

    obj["LineWidth"] = static_cast<double>(LineWidth);
    obj["ConstructionLineWidth"] = static_cast<double>(ConstructionLineWidth);
    obj["PointDiameter"] = static_cast<double>(PointDiameter);
    obj["UseDataDefinedColors"] = UseDataDefinedColors;

    SwJsonObject root;
    root["ConnectionStyle"] = obj;
    return root;
}

} // namespace SwizioNodes
