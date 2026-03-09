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

#pragma once

/**
 * @file src/core/gui/StyleSheet.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by StyleSheet in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the style sheet interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are StyleSheet.
 *
 * Style-oriented declarations here capture reusable visual parameters so rendering code can stay
 * deterministic while still allowing higher-level customization.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <stdexcept>

#include "Sw.h"
#include "SwString.h"
#include "SwMap.h"



class StyleSheet {
public:
    // Un dictionnaire qui mappe des sélecteurs CSS à des styles (propriétés)
    SwMap<SwString, SwMap<SwString, SwString>> styles;

    /**
     * @brief Constructs a `StyleSheet` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    StyleSheet()
    {
        colorNames = {
               {"red", makeColor(255, 0, 0)},
               {"green", makeColor(0, 255, 0)},
               {"blue", makeColor(0, 0, 255)},
               {"yellow", makeColor(255, 255, 0)},
               {"black", makeColor(0, 0, 0)},
               {"white", makeColor(255, 255, 255)},
               {"gray", makeColor(128, 128, 128)},
               {"cyan", makeColor(0, 255, 255)},
               {"magenta", makeColor(255, 0, 255)},
               {"orange", makeColor(255, 165, 0)},
               {"purple", makeColor(128, 0, 128)},
               {"brown", makeColor(165, 42, 42)},
               {"pink", makeColor(255, 192, 203)},
               {"lime", makeColor(0, 255, 0)},
               {"olive", makeColor(128, 128, 0)},
               {"navy", makeColor(0, 0, 128)},
               {"teal", makeColor(0, 128, 128)},
               {"maroon", makeColor(128, 0, 0)},
               {"silver", makeColor(192, 192, 192)},
               {"gold", makeColor(255, 215, 0)}
        };
    }
    // Méthode pour décoder une ligne CSS
    /**
     * @brief Performs the `parseStyleSheet` operation.
     * @param css Value passed to the method.
     */
    void parseStyleSheet(const SwString& css) {
        std::string css_str = css.toStdString();
        std::string normalized;
        normalized.reserve(css_str.size() * 2);
        for (char ch : css_str) {
            if (ch == '{') {
                normalized.push_back('{');
                normalized.push_back('\n');
            } else if (ch == '}') {
                normalized.push_back('\n');
                normalized.push_back('}');
                normalized.push_back('\n');
            } else if (ch == ';') {
                normalized.push_back(';');
                normalized.push_back('\n');
            } else {
                normalized.push_back(ch);
            }
        }

        std::istringstream stream(normalized);
        std::string line;
        std::string currentSelector;

        // Lire ligne par ligne le contenu du style CSS
        while (std::getline(stream, line)) {
            line = trim(line);  // Supprimer les espaces
            if (line.empty()) continue;

            // Vérifier si la ligne est un sélecteur CSS (comme "Button {")
            if (line.back() == '{') {
                currentSelector = trim(line.substr(0, line.size() - 1));
            }
            else if (line == "}") {
                currentSelector.clear();
            }
            else if (!currentSelector.empty()) {
                auto parts = split(line, ':');
                if (parts.size() == 2) {
                    std::string property = trim(parts[0]);
                    std::string value = trim(parts[1]);
                    value = value.substr(0, value.size() - 1);  // Retirer le ; à la fin de la propriété
                    styles[SwString(currentSelector)][SwString(property)] = SwString(value);
                }
            }
        }
    }

    // Récupérer une propriété spécifique pour un sélecteur donné
    SwString getStyleProperty(const SwString& selector, const SwString& property) const {
        auto selectorIt = styles.find(selector);
        if (selectorIt != styles.end()) {
            const auto& props = selectorIt->second;
            auto propIt = props.find(property);
            if (propIt != props.end()) {
                return propIt->second;
            }
        }
        return SwString();
    }



    // Convertir une couleur CSS en COLORREF (hex, rgb(), nom de couleur)
    SwColor parseColor(const SwString& color, float* alphaOut = nullptr) {
        if (alphaOut) {
            *alphaOut = 1.0f;
        }
        std::string trimmedColor = trim(color.toStdString());

        if (trimmedColor.empty()) {
            return makeColor(0, 0, 0);
        }

        if (trimmedColor == "transparent") {
            if (alphaOut) {
                *alphaOut = 0.0f;
            }
            return makeColor(0, 0, 0);
        }

        // Gérer un gradient en utilisant la première couleur comme fallback.
        if (trimmedColor.find("linear-gradient") != std::string::npos) {
            size_t open = trimmedColor.find('(');
            size_t close = trimmedColor.rfind(')');
            if (open != std::string::npos && close != std::string::npos && close > open + 1) {
                std::string inner = trimmedColor.substr(open + 1, close - open - 1);
                auto commaPos = inner.find(',');
                std::string firstColor = (commaPos != std::string::npos) ? inner.substr(0, commaPos) : inner;
                return parseColor(SwString(firstColor), alphaOut);
            }
        }

        // Vérifier si la couleur est au format hexadécimal (#RRGGBB)
        if (trimmedColor[0] == '#') {
            if (trimmedColor.length() == 7) {
                unsigned int rgb;
                std::stringstream ss;
                ss << std::hex << trimmedColor.substr(1);  // Ignorer le '#'
                ss >> rgb;

                return makeColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
            }
            else {
                throw std::invalid_argument("Invalid hex color format. Expected #RRGGBB.");
            }
        }

        // Vérifier si la couleur est au format rgba()
        if (trimmedColor.find("rgba(") == 0 && trimmedColor.back() == ')') {
            std::string rgbaValues = trimmedColor.substr(5, trimmedColor.size() - 6);
            std::stringstream ss(rgbaValues);
            std::string item;
            int rgba[3], i = 0;
            while (i < 3 && std::getline(ss, item, ',')) {
                rgba[i] = std::stoi(trim(item));
                rgba[i] = std::max(0, std::min(255, rgba[i]));
                ++i;
            }
            if (i == 3) {
                std::string alphaPart;
                if (std::getline(ss, alphaPart, ',')) {
                    alphaPart = trim(alphaPart);
                    float alpha = 1.0f;
                    try {
                        alpha = std::stof(alphaPart);
                    } catch (...) {
                        alpha = 1.0f;
                    }
                    if (alpha > 1.0f) {
                        alpha = alpha / 255.0f;
                    }
                    alpha = std::max(0.0f, std::min(1.0f, alpha));
                    if (alphaOut) {
                        *alphaOut = alpha;
                    }
                    return makeColor(rgba[0], rgba[1], rgba[2]);
                }
            }
            throw std::invalid_argument("Invalid rgba() format. Expected rgba(R, G, B, A).");
        }

        // Vérifier si la couleur est au format rgb()
        if (trimmedColor.find("rgb(") == 0 && trimmedColor.back() == ')') {
            std::string rgbValues = trimmedColor.substr(4, trimmedColor.size() - 5);  // Extraire le contenu entre rgb( et )
            std::stringstream ss(rgbValues);
            std::string item;
            int rgb[3], i = 0;

            while (std::getline(ss, item, ',') && i < 3) {
                rgb[i] = std::stoi(trim(item));
                rgb[i] = std::max(0, std::min(255, rgb[i]));
                ++i;
            }

            if (i == 3) {
                if (alphaOut) {
                    *alphaOut = 1.0f;
                }
                return makeColor(rgb[0], rgb[1], rgb[2]);
            }
            else {
                throw std::invalid_argument("Invalid rgb() format. Expected rgb(R, G, B).");
            }
        }
        auto it = colorNames.find(SwString(trimmedColor));
        if (it != colorNames.end()) {
            return it->second;
        }

        return makeColor(0, 0, 0);
    }

private:
    std::string trim(const std::string& s) {
        std::string result = s;

        // Supprimer les commentaires de style /* ... */
        size_t startComment = result.find("/*");
        while (startComment != std::string::npos) {
            size_t endComment = result.find("*/", startComment + 2);
            if (endComment != std::string::npos) {
                result.erase(startComment, endComment - startComment + 2);  // Supprimer le commentaire
            }
            else {
                result.erase(startComment);  // Si le commentaire n'est pas fermé, on supprime tout à partir de /* 
            }
            startComment = result.find("/*");
        }

        // Supprimer les commentaires de style // 
        size_t commentPos = result.find("//");
        if (commentPos != std::string::npos) {
            result = result.substr(0, commentPos);  // Supprimer tout ce qui suit //
        }

        // Supprimer les espaces en début et en fin de chaîne
        size_t start = result.find_first_not_of(" \t\n\r");
        size_t end = result.find_last_not_of(" \t\n\r");

        return (start == std::string::npos) ? "" : result.substr(start, end - start + 1);
    }


    // Sépare une chaîne en utilisant un délimiteur donné
    std::vector<std::string> split(const std::string& s, char delimiter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }

    // Gérer les noms de couleurs CSS courants
     SwMap<SwString, SwColor> colorNames;

     static SwColor makeColor(int r, int g, int b) {
         SwColor c;
         c.r = r;
         c.g = g;
         c.b = b;
         return c;
     }
};
