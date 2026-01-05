#include "ConnectionStyle.hpp"

#include "StyleCollection.hpp"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QString>

#include "core/gui/SwizioNodes/internal/ColorUtils.hpp"
#include "core/types/Sw.h"
#include "core/types/SwDebug.h"
#include "core/types/SwJsonArray.h"
#include "core/types/SwJsonDocument.h"
#include "core/types/SwJsonObject.h"
#include "core/types/SwJsonValue.h"
#include "core/types/SwString.h"

#include <random>

using QtNodes::ConnectionStyle;

namespace {

inline SwColor toSwColor(const QColor &color)
{
    return SwColor{color.red(), color.green(), color.blue()};
}

inline QColor toQColor(const SwColor &color)
{
    return QColor(color.r, color.g, color.b);
}

SwJsonValue toSwJsonValue(const QJsonValue &value);

SwJsonArray toSwJsonArray(const QJsonArray &array)
{
    SwJsonArray out;
    for (const QJsonValue &value : array) {
        out.append(toSwJsonValue(value));
    }
    return out;
}

SwJsonObject toSwJsonObject(const QJsonObject &object)
{
    SwJsonObject out;
    for (auto it = object.begin(); it != object.end(); ++it) {
        out[it.key().toStdString()] = toSwJsonValue(it.value());
    }
    return out;
}

SwJsonValue toSwJsonValue(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return SwJsonValue();
    case QJsonValue::Bool:
        return SwJsonValue(value.toBool());
    case QJsonValue::Double:
        return SwJsonValue(value.toDouble());
    case QJsonValue::String:
        return SwJsonValue(value.toString().toStdString());
    case QJsonValue::Array:
        return SwJsonValue(toSwJsonArray(value.toArray()));
    case QJsonValue::Object:
        return SwJsonValue(toSwJsonObject(value.toObject()));
    }
    return SwJsonValue();
}

void applySwJson(const SwJsonObject &json,
                 QColor &constructionColor,
                 QColor &normalColor,
                 QColor &selectedColor,
                 QColor &selectedHaloColor,
                 QColor &hoveredColor,
                 float &lineWidth,
                 float &constructionLineWidth,
                 float &pointDiameter,
                 bool &useDataDefinedColors)
{
    const SwJsonObject *obj = &json;
    SwJsonObject sub;
    if (json.contains("ConnectionStyle") && json["ConnectionStyle"].isObject()
        && json["ConnectionStyle"].toObject()) {
        sub = SwJsonObject(json["ConnectionStyle"].toObject());
        obj = &sub;
    }

    auto readColor = [&](const char *key, QColor &target) {
        if (!obj->contains(key)) {
#ifdef STYLE_DEBUG
            swWarning() << "Undefined value for parameter:" << key;
#endif
            return;
        }
        target = toQColor(SwizioNodes::parseColorValue_((*obj)[key], toSwColor(target)));
    };
    auto readFloat = [&](const char *key, float &target) {
        if (!obj->contains(key)) {
#ifdef STYLE_DEBUG
            swWarning() << "Undefined value for parameter:" << key;
#endif
            return;
        }
        target = static_cast<float>((*obj)[key].toDouble());
    };

    readColor("ConstructionColor", constructionColor);
    readColor("NormalColor", normalColor);
    readColor("SelectedColor", selectedColor);
    readColor("SelectedHaloColor", selectedHaloColor);
    readColor("HoveredColor", hoveredColor);

    readFloat("LineWidth", lineWidth);
    readFloat("ConstructionLineWidth", constructionLineWidth);
    readFloat("PointDiameter", pointDiameter);

    if (obj->contains("UseDataDefinedColors")) {
        useDataDefinedColors = (*obj)["UseDataDefinedColors"].toBool();
    }
}

} // namespace

ConnectionStyle::ConnectionStyle()
{
    ConstructionColor = QColor(128, 128, 128);
    NormalColor = QColor(0, 139, 139);
    SelectedColor = QColor(100, 100, 100);
    SelectedHaloColor = QColor(255, 165, 0);
    HoveredColor = QColor(224, 255, 255);

    LineWidth = 3.0f;
    ConstructionLineWidth = 2.0f;
    PointDiameter = 10.0f;

    UseDataDefinedColors = false;
}

ConnectionStyle::ConnectionStyle(QString jsonText)
    : ConnectionStyle()
{
    if (jsonText.isEmpty()) {
        return;
    }
    SwJsonDocument doc = SwJsonDocument::fromJson(jsonText.toUtf8().toStdString());
    applySwJson(doc.object(),
                ConstructionColor,
                NormalColor,
                SelectedColor,
                SelectedHaloColor,
                HoveredColor,
                LineWidth,
                ConstructionLineWidth,
                PointDiameter,
                UseDataDefinedColors);
}

void ConnectionStyle::setConnectionStyle(QString jsonText)
{
    ConnectionStyle style(jsonText);

    StyleCollection::setConnectionStyle(style);
}

void ConnectionStyle::loadJson(QJsonObject const &json)
{
    applySwJson(toSwJsonObject(json),
                ConstructionColor,
                NormalColor,
                SelectedColor,
                SelectedHaloColor,
                HoveredColor,
                LineWidth,
                ConstructionLineWidth,
                PointDiameter,
                UseDataDefinedColors);
}

QJsonObject ConnectionStyle::toJson() const
{
    QJsonObject obj;

    obj["ConstructionColor"] = QString::fromStdString(SwizioNodes::toHex_(toSwColor(ConstructionColor)));
    obj["NormalColor"] = QString::fromStdString(SwizioNodes::toHex_(toSwColor(NormalColor)));
    obj["SelectedColor"] = QString::fromStdString(SwizioNodes::toHex_(toSwColor(SelectedColor)));
    obj["SelectedHaloColor"] = QString::fromStdString(SwizioNodes::toHex_(toSwColor(SelectedHaloColor)));
    obj["HoveredColor"] = QString::fromStdString(SwizioNodes::toHex_(toSwColor(HoveredColor)));

    obj["LineWidth"] = LineWidth;
    obj["ConstructionLineWidth"] = ConstructionLineWidth;
    obj["PointDiameter"] = PointDiameter;
    obj["UseDataDefinedColors"] = UseDataDefinedColors;

    QJsonObject root;
    root["ConnectionStyle"] = obj;

    return root;
}

QColor ConnectionStyle::constructionColor() const
{
    return ConstructionColor;
}

QColor ConnectionStyle::normalColor() const
{
    return NormalColor;
}

QColor ConnectionStyle::normalColor(QString typeId) const
{
    std::size_t hash = qHash(typeId);

    std::size_t const hue_range = 0xFF;

    std::mt19937 gen(static_cast<unsigned int>(hash));
    std::uniform_int_distribution<int> distrib(0, hue_range);

    int hue = distrib(gen);
    int sat = 120 + hash % 129;

    return QColor::fromHsl(hue, sat, 160);
}

QColor ConnectionStyle::selectedColor() const
{
    return SelectedColor;
}

QColor ConnectionStyle::selectedHaloColor() const
{
    return SelectedHaloColor;
}

QColor ConnectionStyle::hoveredColor() const
{
    return HoveredColor;
}

float ConnectionStyle::lineWidth() const
{
    return LineWidth;
}

float ConnectionStyle::constructionLineWidth() const
{
    return ConstructionLineWidth;
}

float ConnectionStyle::pointDiameter() const
{
    return PointDiameter;
}

bool ConnectionStyle::useDataDefinedColors() const
{
    return UseDataDefinedColors;
}
