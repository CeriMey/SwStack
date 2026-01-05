#pragma once

#include "Export.hpp"
#include "Style.hpp"

#include "ColorUtils.hpp"

#include "core/types/Sw.h"
#include "core/types/SwString.h"

namespace SwizioNodes {

class SWIZIO_NODES_PUBLIC GraphicsViewStyle : public Style
{
public:
    GraphicsViewStyle()
    {
        // Defaults mirror `exemples/33-Nodeeditor/resources/DefaultStyle.json`.
        BackgroundColor = SwColor{53, 53, 53};
        FineGridColor = SwColor{60, 60, 60};
        CoarseGridColor = SwColor{25, 25, 25};
    }

    explicit GraphicsViewStyle(SwString jsonText) { loadJsonText(std::move(jsonText)); }

    ~GraphicsViewStyle() override = default;

public:
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
    SwJsonObject obj = json.contains("GraphicsViewStyle") && json["GraphicsViewStyle"].isObject() && json["GraphicsViewStyle"].toObject()
                           ? SwJsonObject(json["GraphicsViewStyle"].toObject())
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
