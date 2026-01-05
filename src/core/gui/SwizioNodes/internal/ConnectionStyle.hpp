#pragma once

#include "Export.hpp"
#include "Style.hpp"

#include "ColorUtils.hpp"

#include "core/types/Sw.h"
#include "core/types/SwString.h"

namespace SwizioNodes {

class SWIZIO_NODES_PUBLIC ConnectionStyle : public Style
{
public:
    ConnectionStyle();

    explicit ConnectionStyle(SwString jsonText) { loadJsonText(std::move(jsonText)); }

    ~ConnectionStyle() override = default;

public:
    static void setConnectionStyle(SwString jsonText);

public:
    void loadJson(SwJsonObject const& json) override;

    SwJsonObject toJson() const override;

public:
    SwColor constructionColor() const { return ConstructionColor; }
    SwColor normalColor() const { return NormalColor; }
    SwColor normalColor(SwString /*typeId*/) const { return NormalColor; }
    SwColor selectedColor() const { return SelectedColor; }
    SwColor selectedHaloColor() const { return SelectedHaloColor; }
    SwColor hoveredColor() const { return HoveredColor; }

    float lineWidth() const { return LineWidth; }
    float constructionLineWidth() const { return ConstructionLineWidth; }
    float pointDiameter() const { return PointDiameter; }

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
    SwJsonObject obj = json.contains("ConnectionStyle") && json["ConnectionStyle"].isObject() && json["ConnectionStyle"].toObject()
                           ? SwJsonObject(json["ConnectionStyle"].toObject())
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
