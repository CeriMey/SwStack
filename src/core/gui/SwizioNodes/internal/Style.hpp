#pragma once

#include "core/io/SwFile.h"
#include "core/types/SwJsonDocument.h"
#include "core/types/SwJsonObject.h"
#include "core/types/SwString.h"
#include "core/types/SwDebug.h"

namespace SwizioNodes {

class Style
{
public:
    virtual ~Style() = default;

public:
    virtual void loadJson(SwJsonObject const& json) = 0;

    virtual SwJsonObject toJson() const = 0;

    /// Loads from utf-8 string.
    virtual void loadJsonFromUtf8(const std::string& utf8Json)
    {
        SwJsonDocument doc = SwJsonDocument::fromJson(utf8Json);
        loadJson(doc.object());
    }

    virtual void loadJsonText(SwString jsonText) { loadJsonFromUtf8(jsonText.toStdString()); }

    virtual void loadJsonFile(SwString fileName)
    {
        SwFile file(fileName);
        if (!file.open(SwFile::Read)) {
            swWarning() << "Couldn't open file " << fileName;
            return;
        }

        SwString content = file.readAll();
        loadJsonFromUtf8(content.toStdString());
    }
};

} // namespace SwizioNodes

