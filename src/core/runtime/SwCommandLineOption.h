#pragma once

/**
 * @file src/core/runtime/SwCommandLineOption.h
 * @ingroup core_runtime
 * @brief Declares the public interface exposed by SwCommandLineOption in the CoreSw runtime
 * layer.
 *
 * This header belongs to the CoreSw runtime layer. It coordinates application lifetime, event
 * delivery, timers, threads, crash handling, and other process-level services consumed by the
 * rest of the stack.
 *
 * Within that layer, this file focuses on the command line option interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwCommandLineOption.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Runtime declarations in this area define lifecycle and threading contracts that higher-level
 * modules depend on for safe execution and orderly shutdown.
 *
 */

/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include "SwString.h"
#include "SwList.h"

// Classe SwCommandLineOption pour définir des options de ligne de commande
class SwCommandLineOption {
public:
    // Constructeurs
    /**
     * @brief Constructs a `SwCommandLineOption` instance.
     * @param name Value passed to the method.
     * @param description Value passed to the method.
     * @param valueName Value passed to the method.
     * @param defaultValue Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwCommandLineOption(const SwString& name, const SwString& description,
                        const SwString& valueName = SwString(), const SwString& defaultValue = SwString())
        : names(SwList<SwString>() << name),
          description(description),
          valueName(valueName),
          defaultValues(defaultValue.isEmpty() ? SwList<SwString>() : SwList<SwString>() << defaultValue) {}

    /**
     * @brief Constructs a `SwCommandLineOption` instance.
     * @param names Value passed to the method.
     * @param description Value passed to the method.
     * @param valueName Value passed to the method.
     * @param defaultValues Value passed to the method.
     * @param defaultValues Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwCommandLineOption(const SwList<SwString>& names, const SwString& description,
                        const SwString& valueName = SwString(), const SwList<SwString>& defaultValues = SwList<SwString>())
        : names(names),
          description(description),
          valueName(valueName),
          defaultValues(defaultValues) {}

    // Ajouter un nom court ou long
    /**
     * @brief Adds the specified name.
     * @param name Value passed to the method.
     */
    void addName(const SwString& name) {
        if (!names.contains(name)) {
            names.append(name);
        }
    }

    // Obtenir les noms (long et court)
    /**
     * @brief Returns the current names.
     * @return The current names.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwList<SwString> getNames() const {
        return names;
    }

    // Définir une valeur par défaut
    /**
     * @brief Sets the default Value.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDefaultValue(const SwString& value) {
        defaultValues = SwList<SwString>() << value;
    }

    // Définir plusieurs valeurs par défaut (pour les options multiples)
    /**
     * @brief Sets the default Values.
     * @param values Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDefaultValues(const SwList<SwString>& values) {
        defaultValues = values;
    }

    // Obtenir la valeur par défaut
    /**
     * @brief Returns the current default Values.
     * @return The current default Values.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwList<SwString> getDefaultValues() const {
        return defaultValues;
    }

    // Vérifier si l'option nécessite une valeur
    /**
     * @brief Returns whether the object reports value Required.
     * @return `true` when the object reports value Required; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isValueRequired() const {
        return !valueName.isEmpty();
    }

    // Obtenir la description
    /**
     * @brief Returns the current description.
     * @return The current description.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString getDescription() const {
        return description;
    }

    // Obtenir le nom de la valeur (pour usage dans les messages d'aide)
    /**
     * @brief Returns the current value Name.
     * @return The current value Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString getValueName() const {
        return valueName;
    }

private:
    SwList<SwString> names;            // Liste des noms (longs et courts)
    SwString description;              // Description pour le texte d'aide
    SwString valueName;                // Nom de la valeur attendue (si applicable)
    SwList<SwString> defaultValues;    // Valeurs par défaut (peut être vide)
};
