#pragma once

/**
 * @file src/core/runtime/SwCommandLineParser.h
 * @ingroup core_runtime
 * @brief Declares the public interface exposed by SwCommandLineParser in the CoreSw runtime
 * layer.
 *
 * This header belongs to the CoreSw runtime layer. It coordinates application lifetime, event
 * delivery, timers, threads, crash handling, and other process-level services consumed by the
 * rest of the stack.
 *
 * Within that layer, this file focuses on the command line parser interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwCommandLineParser.
 *
 * Parser-oriented declarations here are designed so that the implementation can accept partial
 * input, preserve state between calls, and report structured outcomes without requiring
 * whole-buffer processing.
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

#include "SwCommandLineOption.h"
#include "SwCoreApplication.h"
#include "SwString.h"
#include "SwList.h"
#include "SwMap.h"
#include "SwDebug.h"
static constexpr const char* kSwLogCategory_SwCommandLineParser = "sw.core.runtime.swcommandlineparser";


class SwCommandLineParser {
public:
    /**
     * @brief Constructs a `SwCommandLineParser` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwCommandLineParser()
        : helpOptionAdded(false), appDescription("") {}

    // Définir la description de l'application
    /**
     * @brief Sets the application Description.
     * @param description Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setApplicationDescription(const SwString& description) {
        appDescription = description;
    }

    // Ajouter une option au parser
    /**
     * @brief Adds the specified option.
     * @param option Value passed to the method.
     */
    void addOption(const SwCommandLineOption& option) {
        options.append(option);
    }

    // Ajouter l'option d'aide (comme --help ou -h)
    /**
     * @brief Adds the specified help Option.
     */
    void addHelpOption() {
        if (!helpOptionAdded) {
            SwCommandLineOption help({"h", "help"}, "Displays this help message.");
            addOption(help);
            helpOptionAdded = true;
        }
    }

    // Traiter les arguments à partir de SwCoreApplication
    /**
     * @brief Performs the `process` operation.
     * @param app Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool process(const SwCoreApplication& app) {
        parsedOptions.clear();
        positionalArguments.clear();

        for (const auto& option : options) {
            // Traiter les options longues
            for (const auto& name : option.getNames()) {
                if (app.hasArgument(name)) {
                    SwString value = app.getArgument(name);
                    if (value.isEmpty() && option.isValueRequired()) {
                        errorMessage = SwString("Option '--") + name + SwString("' requires a value.");
                        return false;
                    }
                    parsedOptions[name] = value;
                }
            }
        }

        // Récupérer les arguments positionnels
        SwList<SwString> allArguments = app.getPositionalArguments();
        for (const auto& arg : allArguments) {
            if (!arg.startsWith("-")) {
                positionalArguments.append(arg);
            }
        }

        // Vérifier si l'option d'aide est activée
        if (isSet("help")) {
            swCDebug(kSwLogCategory_SwCommandLineParser) << generateHelpText().toStdString();
            std::exit(0);
        }

        return true;
    }

    // Vérifie si une option a été spécifiée
    /**
     * @brief Returns whether the object reports set.
     * @param key Value passed to the method.
     * @return `true` when the object reports set; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isSet(const SwString& key) const {
        for (const auto& option : options) {
            if (option.getNames().contains(key)) {
                for (const auto& name : option.getNames()) {
                    if (parsedOptions.contains(name)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Récupère la valeur d'une option
    /**
     * @brief Performs the `value` operation.
     * @param key Value passed to the method.
     * @param defaultValue Value passed to the method.
     * @return The requested value.
     */
    SwString value(const SwString& key, const SwString& defaultValue = "") const {
        for (const auto& option : options) {
            if (option.getNames().contains(key)) {
                for (const auto& name : option.getNames()) {
                    if (parsedOptions.contains(name)) {
                        return parsedOptions[name]; // Retourne la première valeur trouvée
                    }
                }
            }
        }
        return defaultValue; // Retourne la valeur par défaut si aucun nom ne correspond
    }


    // Récupère les arguments positionnels
    /**
     * @brief Returns the current positional Arguments List.
     * @return The current positional Arguments List.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwList<SwString> positionalArgumentsList() const {
        return positionalArguments;
    }

    // Génère le texte d'aide
    /**
     * @brief Returns the current generate Help Text.
     * @return The current generate Help Text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString generateHelpText() const {
        SwString result;

        if (!appDescription.isEmpty()) {
            result += appDescription + "\n\n";
        }

        result += "Options:\n";

        for (const auto& option : options) {
            SwString names = SwString("--") + option.getNames().first();
            if (option.getNames().size() > 1) {
                names += SwString(", -") + option.getNames().last();
            }
            if (!option.getValueName().isEmpty()) {
                names += SwString(" <") + option.getValueName() + ">";
            }

            result += SwString("  ") + names + SwString("\n    ") + option.getDescription() + SwString("\n");
        }

        return result;
    }

    // Récupérer le message d'erreur (en cas d'échec du parsing)
    /**
     * @brief Returns the current error.
     * @return The current error.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString error() const {
        return errorMessage;
    }

private:
    SwList<SwCommandLineOption> options;     // Liste des options possibles
    SwMap<SwString, SwString> parsedOptions; // Options parsées
    SwList<SwString> positionalArguments;   // Arguments positionnels
    SwString appDescription;                // Description de l'application
    SwString errorMessage;                  // Message d'erreur en cas de problème
    bool helpOptionAdded;                   // Indique si l'option d'aide a été ajoutée
};
