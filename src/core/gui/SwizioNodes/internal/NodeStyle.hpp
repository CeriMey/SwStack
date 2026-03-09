#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/NodeStyle.hpp
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by NodeStyle in the CoreSw node-editor layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the node style interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
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

class SWIZIO_NODES_PUBLIC NodeStyle : public Style
{
public:
    /**
     * @brief Constructs a `NodeStyle` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    NodeStyle();

    /**
     * @brief Constructs a `NodeStyle` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit NodeStyle(SwString jsonText) { loadJsonText(std::move(jsonText)); }

    /**
     * @brief Constructs a `NodeStyle` instance.
     * @param json Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit NodeStyle(SwJsonObject const& json) { loadJson(json); }

    /**
     * @brief Destroys the `NodeStyle` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~NodeStyle() override = default;

public:
    /**
     * @brief Sets the node Style.
     * @param jsonText Value passed to the method.
     * @return The requested node Style.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    static void setNodeStyle(SwString jsonText);

    /**
     * @brief Sets the widget Style.
     * @param jsonText Value passed to the method.
     * @return The requested widget Style.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    static void setWidgetStyle(SwString jsonText);

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
    SwColor NormalBoundaryColor;
    SwColor SelectedBoundaryColor;
    SwColor GradientColor0;
    SwColor GradientColor1;
    SwColor GradientColor2;
    SwColor GradientColor3;
    SwColor ShadowColor;
    SwColor FontColor;
    SwColor FontColorFaded;

    SwColor ConnectionPointColor;
    SwColor FilledConnectionPointColor;

    SwColor WarningColor;
    SwColor ErrorColor;

    float PenWidth;
    float HoveredPenWidth;

    float ConnectionPointDiameter;

    float Opacity;
};

inline NodeStyle::NodeStyle()
{
    // Defaults mirror `exemples/33-Nodeeditor/resources/DefaultStyle.json`.
    NormalBoundaryColor = SwColor{255, 255, 255};
    SelectedBoundaryColor = SwColor{255, 165, 0};
    GradientColor0 = SwColor{128, 128, 128};
    GradientColor1 = SwColor{80, 80, 80};
    GradientColor2 = SwColor{64, 64, 64};
    GradientColor3 = SwColor{58, 58, 58};
    ShadowColor = SwColor{20, 20, 20};
    FontColor = SwColor{255, 255, 255};
    FontColorFaded = SwColor{128, 128, 128};

    ConnectionPointColor = SwColor{169, 169, 169};
    FilledConnectionPointColor = SwColor{0, 255, 255};

    WarningColor = SwColor{128, 128, 0};
    ErrorColor = SwColor{255, 0, 0};

    PenWidth = 1.0f;
    HoveredPenWidth = 1.5f;
    ConnectionPointDiameter = 8.0f;
    Opacity = 0.8f;
}

inline void NodeStyle::loadJson(SwJsonObject const& json)
{
    SwJsonObject obj = json.contains("NodeStyle") && json["NodeStyle"].isObject()
                           ? json["NodeStyle"].toObject()
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

    readColor("NormalBoundaryColor", NormalBoundaryColor);
    readColor("SelectedBoundaryColor", SelectedBoundaryColor);
    readColor("GradientColor0", GradientColor0);
    readColor("GradientColor1", GradientColor1);
    readColor("GradientColor2", GradientColor2);
    readColor("GradientColor3", GradientColor3);
    readColor("ShadowColor", ShadowColor);
    readColor("FontColor", FontColor);
    readColor("FontColorFaded", FontColorFaded);
    readColor("ConnectionPointColor", ConnectionPointColor);
    readColor("FilledConnectionPointColor", FilledConnectionPointColor);
    readColor("WarningColor", WarningColor);
    readColor("ErrorColor", ErrorColor);

    readFloat("PenWidth", PenWidth);
    readFloat("HoveredPenWidth", HoveredPenWidth);
    readFloat("ConnectionPointDiameter", ConnectionPointDiameter);
    readFloat("Opacity", Opacity);
}

inline SwJsonObject NodeStyle::toJson() const
{
    SwJsonObject obj;

    obj["NormalBoundaryColor"] = toHex_(NormalBoundaryColor);
    obj["SelectedBoundaryColor"] = toHex_(SelectedBoundaryColor);
    obj["GradientColor0"] = toHex_(GradientColor0);
    obj["GradientColor1"] = toHex_(GradientColor1);
    obj["GradientColor2"] = toHex_(GradientColor2);
    obj["GradientColor3"] = toHex_(GradientColor3);
    obj["ShadowColor"] = toHex_(ShadowColor);
    obj["FontColor"] = toHex_(FontColor);
    obj["FontColorFaded"] = toHex_(FontColorFaded);

    obj["ConnectionPointColor"] = toHex_(ConnectionPointColor);
    obj["FilledConnectionPointColor"] = toHex_(FilledConnectionPointColor);

    obj["WarningColor"] = toHex_(WarningColor);
    obj["ErrorColor"] = toHex_(ErrorColor);

    obj["PenWidth"] = static_cast<double>(PenWidth);
    obj["HoveredPenWidth"] = static_cast<double>(HoveredPenWidth);
    obj["ConnectionPointDiameter"] = static_cast<double>(ConnectionPointDiameter);
    obj["Opacity"] = static_cast<double>(Opacity);

    SwJsonObject root;
    root["NodeStyle"] = obj;
    return root;
}

} // namespace SwizioNodes
