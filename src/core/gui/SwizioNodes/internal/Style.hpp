#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/Style.hpp
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by Style in the CoreSw node-editor layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the style interface. The declarations exposed here
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


#include "core/io/SwFile.h"
#include "core/types/SwJsonDocument.h"
#include "core/types/SwJsonObject.h"
#include "core/types/SwString.h"
#include "core/types/SwDebug.h"

namespace SwizioNodes {

class Style
{
public:
    /**
     * @brief Destroys the `Style` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~Style() = default;

public:
    /**
     * @brief Performs the `loadJson` operation on the associated resource.
     * @param json Value passed to the method.
     * @return The resulting json.
     */
    virtual void loadJson(SwJsonObject const& json) = 0;

    /**
     * @brief Returns the current to Json.
     * @return The current to Json.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwJsonObject toJson() const = 0;

    /// Loads from utf-8 string.
    virtual void loadJsonFromUtf8(const std::string& utf8Json)
    {
        SwJsonDocument doc = SwJsonDocument::fromJson(utf8Json);
        loadJson(doc.object());
    }

    /**
     * @brief Performs the `loadJsonText` operation on the associated resource.
     * @return The resulting json Text.
     */
    virtual void loadJsonText(SwString jsonText) { loadJsonFromUtf8(jsonText.toStdString()); }

    /**
     * @brief Performs the `loadJsonFile` operation on the associated resource.
     * @param fileName Value passed to the method.
     * @return The resulting json File.
     */
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
