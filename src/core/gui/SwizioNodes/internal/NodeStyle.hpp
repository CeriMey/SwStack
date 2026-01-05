#pragma once

#include "Export.hpp"
#include "Style.hpp"

#include "ColorUtils.hpp"

#include "core/types/Sw.h"
#include "core/types/SwString.h"

namespace SwizioNodes {

class SWIZIO_NODES_PUBLIC NodeStyle : public Style
{
public:
    NodeStyle();

    explicit NodeStyle(SwString jsonText) { loadJsonText(std::move(jsonText)); }

    explicit NodeStyle(SwJsonObject const& json) { loadJson(json); }

    ~NodeStyle() override = default;

public:
    static void setNodeStyle(SwString jsonText);

    static void setWidgetStyle(SwString jsonText);

public:
    void loadJson(SwJsonObject const& json) override;

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
    SwJsonObject obj = json.contains("NodeStyle") && json["NodeStyle"].isObject() && json["NodeStyle"].toObject()
                           ? SwJsonObject(json["NodeStyle"].toObject())
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
