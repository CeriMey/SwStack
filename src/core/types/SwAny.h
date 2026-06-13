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
 * @file src/core/types/SwAny.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwAny in the CoreSw fundamental types layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the any interface. The declarations exposed here define
 * the stable surface that adjacent code can rely on while the implementation remains free to
 * evolve behind the header.
 *
 * The main declarations in this header are SwAny.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */


#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>
#include <typeinfo>
#include <vector>
#include <cctype>
#include <limits>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <mutex>
#include <sstream>
#include "SwString.h"
#include "SwJsonValue.h"
#include "SwJsonObject.h"
#include "SwJsonArray.h"
#include "SwJsonDocument.h"
#include "SwList.h"
#include "SwMetaType.h"
#include "Sw.h"
#include <map>
#include <functional>

#include "SwDebug.h"
static constexpr const char* kSwLogCategory_SwAny = "sw.core.types.swany";




class SwAny {

protected:
    static std::uint64_t parseUInt64_(const SwString& value, bool* ok = nullptr) {
        const std::string text = value.trimmed().toStdString();
        if (text.empty() || text[0] == '-') {
            if (ok) *ok = false;
            return 0;
        }

        try {
            std::size_t parsedChars = 0;
            const unsigned long long parsed = std::stoull(text, &parsedChars, 0);
            if (parsedChars != text.size()) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<std::uint64_t>(parsed);
        } catch (...) {
            if (ok) *ok = false;
            return 0;
        }
    }

    /**
     * @brief Returns the current register All Type Once.
     * @return The current register All Type Once.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static bool registerAllTypeOnce() {
        static bool oneCheck = registerAllType();
        return oneCheck;
    }

    /**
     * @brief Returns the current register All Type.
     * @return The current register All Type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static bool registerAllType() {
        registerMetaType<SwString>();
        registerMetaType<SwJsonValue>();
        registerMetaType<SwJsonObject>();
        registerMetaType<SwJsonArray>();
        registerMetaType<SwJsonDocument>();
        registerMetaType<std::vector<std::string>>();
        registerMetaType<DrawTextFormats>();
        registerMetaType<WindowFlags>();
        registerMetaType<EntryTypes>();
        registerMetaType<CursorType>();
        registerMetaType<FocusPolicyEnum>();
        registerMetaType<EchoModeEnum>();

        registerStringSerialization<CursorType>(
            [](const CursorType& v) -> SwString {
                return SwString(swCursorTypeToString(v));
            },
            [](const SwString& s) -> CursorType {
                return swCursorTypeFromString(s.toStdString());
            });

        registerStringSerialization<FocusPolicyEnum>(
            [](const FocusPolicyEnum& v) -> SwString {
                return SwString(swFocusPolicyToString(v));
            },
            [](const SwString& s) -> FocusPolicyEnum {
                return swFocusPolicyFromString(s.toStdString());
            });

        registerStringSerialization<EchoModeEnum>(
            [](const EchoModeEnum& v) -> SwString {
                return SwString(swEchoModeToString(v));
            },
            [](const SwString& s) -> EchoModeEnum {
                return swEchoModeFromString(s.toStdString());
            });

        SwAny::registerConversion<const char*, SwString>([](const char* cstr) {
            return cstr ? SwString(cstr) : SwString();
        });

        // std::string -> SwString
        SwAny::registerConversion<std::string, SwString>([](const std::string& s) {
            return SwString(s);
        });

        // Conversions depuis SwString vers std::string
        SwAny::registerConversion<SwString, std::string>([](const SwString& s) {
            return s.toStdString();
        });

        // Conversion depuis SwString vers std::vector<uint8_t> (byte array)
        SwAny::registerConversion<SwString, std::vector<uint8_t>>([](const SwString& s) {
            const std::string& strVal = s.toStdString();
            return std::vector<uint8_t>(strVal.begin(), strVal.end());
        });
        SwAny::registerConversion<std::vector<uint8_t>, SwString>([](const std::vector<uint8_t>& bytes) {
            return SwString(std::string(bytes.begin(), bytes.end()));
        });

        // Conversion depuis SwString vers int
        // Nécessite que le contenu du SwString soit convertible (par ex. "123")
        SwAny::registerConversion<SwString, int>([](const SwString& s) {
            return s.toInt();
        });

        SwAny::registerConversion<int, SwString>([](int i) {
            return SwString::number(i);
        });

        SwAny::registerConversion<SwString, long long>([](const SwString& s) {
            return s.toLongLong();
        });
        SwAny::registerConversion<long long, SwString>([](long long v) {
            return SwString::number(v);
        });
        SwAny::registerConversion<SwString, std::int64_t>([](const SwString& s) {
            return static_cast<std::int64_t>(s.toLongLong());
        });
        SwAny::registerConversion<std::int64_t, SwString>([](std::int64_t v) {
            return SwString::number(static_cast<long long>(v));
        });
        SwAny::registerConversion<SwString, std::uint64_t>([](const SwString& s) {
            return parseUInt64_(s);
        });
        SwAny::registerConversion<std::uint64_t, SwString>([](std::uint64_t v) {
            return SwString::number(static_cast<unsigned long long>(v));
        });
        SwAny::registerConversion<SwString, unsigned long long>([](const SwString& s) {
            return static_cast<unsigned long long>(parseUInt64_(s));
        });
        SwAny::registerConversion<unsigned long long, SwString>([](unsigned long long v) {
            return SwString::number(v);
        });
        SwAny::registerConversion<int, long long>([](int v) {
            return static_cast<long long>(v);
        });
        SwAny::registerConversion<long long, int>([](long long v) {
            if (v < static_cast<long long>(std::numeric_limits<int>::min()) ||
                v > static_cast<long long>(std::numeric_limits<int>::max())) {
                return 0;
            }
            return static_cast<int>(v);
        });
        SwAny::registerConversion<int, std::int64_t>([](int v) {
            return static_cast<std::int64_t>(v);
        });
        SwAny::registerConversion<std::int64_t, int>([](std::int64_t v) {
            if (v < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
                v > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
                return 0;
            }
            return static_cast<int>(v);
        });
        SwAny::registerConversion<int, std::uint64_t>([](int v) {
            return v < 0 ? 0ull : static_cast<std::uint64_t>(v);
        });
        SwAny::registerConversion<std::uint64_t, int>([](std::uint64_t v) {
            if (v > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                return 0;
            }
            return static_cast<int>(v);
        });
        SwAny::registerConversion<uint32_t, std::uint64_t>([](uint32_t v) {
            return static_cast<std::uint64_t>(v);
        });
        SwAny::registerConversion<std::uint64_t, uint32_t>([](std::uint64_t v) {
            if (v > static_cast<std::uint64_t>(std::numeric_limits<uint32_t>::max())) {
                return 0u;
            }
            return static_cast<uint32_t>(v);
        });
        SwAny::registerConversion<long long, std::uint64_t>([](long long v) {
            return v < 0 ? 0ull : static_cast<std::uint64_t>(v);
        });
        SwAny::registerConversion<std::uint64_t, long long>([](std::uint64_t v) {
            if (v > static_cast<std::uint64_t>(std::numeric_limits<long long>::max())) {
                return 0ll;
            }
            return static_cast<long long>(v);
        });

        // Conversion depuis SwString vers float
        SwAny::registerConversion<SwString, float>([](const SwString& s) {
            return s.toFloat();
        });
        SwAny::registerConversion<float, SwString>([](float v) {
            std::ostringstream oss;
            oss.precision(10);
            oss << v;
            return SwString(oss.str());
        });
        // Conversion depuis SwString vers double
        SwAny::registerConversion<SwString, double>([](const SwString& s) {
            return s.toDouble();
        });
        SwAny::registerConversion<double, SwString>([](double v) {
            std::ostringstream oss;
            oss.precision(15);        // précision raisonnable
            oss << v;
            return SwString(oss.str());
        });
        SwAny::registerConversion<SwString, bool>([](const SwString& s) {
            return ((s == "true")? true : false);
        });
        SwAny::registerConversion<bool, SwString>([](bool b) {
            return ((b)?"true":"false");
        });

        // SwString  -> std::vector<std::string>
        // Format attendu: "a,b,c" (les espaces autour sont trim)
        SwAny::registerConversion<SwString, std::vector<std::string>>([](const SwString& s) {
            std::vector<std::string> out;
            auto parts = s.split(',');                 // "a,b,c" -> ["a","b","c"]
            for (int i = 0; i < parts.size(); ++i) {
                out.emplace_back(parts[i].trimmed().toStdString());
            }
            return out;
        });

        // std::vector<std::string> -> SwString
        // Produit: "a,b,c" (pas d’échappement; simple join)
        SwAny::registerConversion<std::vector<std::string>, SwString>([](const std::vector<std::string>& v) {
            SwString res;
            for (size_t i = 0; i < v.size(); ++i) {
                if (i) res.append(",");
                res.append(v[i]);
            }
            return res;
        });





        return true;
    }

    /**
     * @brief Returns the current ensure Registry Initialized.
     * @return The current ensure Registry Initialized.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static void ensureRegistryInitialized() {
        static bool initialized = registerAllTypeOnce();
        (void)initialized;
    }

private:
    // Union pour stocker plusieurs types
    union Storage {
        void* dynamic;
        bool b;
        int i;
        long long i64;
        std::int64_t stdI64;
        unsigned long long u64;
        std::uint64_t stdU64;
        float f;
        double d;
        uint32_t u32;
        std::string str;
        std::vector<uint8_t> byteArray;

        // Constructeur et destructeur de l'union
        Storage() : dynamic(nullptr) {}
        ~Storage() {}
    } storage;

public:
    // Constructeurs pour les types de base et complexes
    /**
     * @brief Constructs a `SwAny` instance.
     * @param value Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(bool value) { ensureRegistryInitialized(); store(value); }
    /**
     * @brief Constructs a `SwAny` instance.
     * @param value Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(int value) { ensureRegistryInitialized(); store(value); }
    /**
     * @brief Constructs a `SwAny` instance.
     * @param value Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(long long value) { ensureRegistryInitialized(); store(value); }
    template <typename T,
              typename std::enable_if<std::is_same<T, std::int64_t>::value &&
                                      !std::is_same<std::int64_t, long long>::value,
                                      int>::type = 0>
    SwAny(T value) { ensureRegistryInitialized(); store(value); }
    /**
     * @brief Constructs a `SwAny` instance.
     * @param value Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(unsigned long long value) { ensureRegistryInitialized(); store(value); }
    template <typename T,
              typename std::enable_if<std::is_same<T, std::uint64_t>::value &&
                                      !std::is_same<std::uint64_t, unsigned long long>::value,
                                      long>::type = 0>
    SwAny(T value) { ensureRegistryInitialized(); store(value); }
    /**
     * @brief Constructs a `SwAny` instance.
     * @param value Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(float value) { ensureRegistryInitialized(); store(value); }
    /**
     * @brief Constructs a `SwAny` instance.
     * @param value Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(double value) { ensureRegistryInitialized(); store(value); }
    /**
     * @brief Constructs a `SwAny` instance.
     * @param value Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(const std::string& value) { ensureRegistryInitialized(); store(value); }
    /**
     * @brief Constructs a `SwAny` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(const char* value) { ensureRegistryInitialized(); store(std::string(value ? value : "")); }
    /**
     * @brief Constructs a `SwAny` instance.
     * @param value Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(const std::vector<uint8_t>& value) { ensureRegistryInitialized(); store(value); }
    /**
     * @brief Constructs a `SwAny` instance.
     * @param value Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(const SwString& value) { ensureRegistryInitialized(); store(value); }
    /**
     * @brief Constructs a `SwAny` instance.
     * @param value Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(const SwByteArray& value) { ensureRegistryInitialized(); store(value); }
    /**
     * @brief Constructs a `SwAny` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(unsigned int value) { ensureRegistryInitialized(); store(static_cast<uint32_t>(value)); }

    /**
     * @brief Constructs a `SwAny` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny(const SwAny& other) {
        ensureRegistryInitialized();
        copyFrom(other);
    }

    SwAny(SwAny&& other) {
        ensureRegistryInitialized();
        moveFrom(std::move(other));
    }

    /**
     * @brief Constructs a `SwAny` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwAny() : typeNameStr("") {
        ensureRegistryInitialized();
    }

    // Opérateur d'assignation pour copier les valeurs
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwAny& operator=(const SwAny& other) {
        if (this != &other) {
            clear();
            copyFrom(other);
        }
        return *this;
    }

    SwAny& operator=(SwAny&& other) {
        if (this != &other) {
            clear();
            moveFrom(std::move(other));
        }
        return *this;
    }

    /**
     * @brief Performs the `operator=` operation.
     * @param bytes Value passed to the method.
     * @return The requested operator =.
     */
    SwAny& operator=(const SwByteArray& bytes) {
        store(bytes);
        return *this;
    }

    /**
     * @brief Performs the `operator=` operation.
     * @param value Value passed to the method.
     * @return The requested operator =.
     */
    SwAny& operator=(unsigned int value) {
        store(static_cast<uint32_t>(value));
        return *this;
    }

    /**
     * @brief Performs the `operator=` operation.
     * @param v Value passed to the method.
     * @return The requested operator =.
     */
    SwAny& operator=(double v) { store(v); return *this; }
    /**
     * @brief Performs the `operator=` operation.
     * @param v Value passed to the method.
     * @return The requested operator =.
     */
    SwAny& operator=(float  v) { store(v); return *this; }
    /**
     * @brief Performs the `operator=` operation.
     * @param v Value passed to the method.
     * @return The requested operator =.
     */
    SwAny& operator=(int    v) { store(v); return *this; }
    /**
     * @brief Performs the `operator=` operation.
     * @param v Value passed to the method.
     * @return The requested operator =.
     */
    SwAny& operator=(long long v) { store(v); return *this; }
    template <typename T,
              typename std::enable_if<std::is_same<T, std::int64_t>::value &&
                                      !std::is_same<std::int64_t, long long>::value,
                                      int>::type = 0>
    SwAny& operator=(T v) { store(v); return *this; }
    /**
     * @brief Performs the `operator=` operation.
     * @param v Value passed to the method.
     * @return The requested operator =.
     */
    SwAny& operator=(unsigned long long v) { store(v); return *this; }
    template <typename T,
              typename std::enable_if<std::is_same<T, std::uint64_t>::value &&
                                      !std::is_same<std::uint64_t, unsigned long long>::value,
                                      long>::type = 0>
    SwAny& operator=(T v) { store(v); return *this; }

    // Destructeur
    /**
     * @brief Destroys the `SwAny` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwAny() { clear(); }


    // Méthode statique pour enregistrer une conversion possible entre deux types
    // On demande un lambda ou une fonction qui explique comment convertir From -> To.
    template<typename From, typename To>
    /**
     * @brief Performs the `registerConversion` operation.
     * @param converterFunc Value passed to the method.
     * @return The requested register Conversion.
     */
    static void registerConversion(std::function<To(const From&)> converterFunc) {
        auto fromName = std::string(typeid(From).name());
        auto toName = std::string(typeid(To).name());
        std::lock_guard<std::mutex> lock(registryMutex());

        // Enregistrer dans la map qu'une conversion de fromName vers toName est possible
        auto& targets = getConversionRules()[fromName];
        bool alreadyRegistered = false;
        for (const auto& target : targets) {
            if (target == toName) {
                alreadyRegistered = true;
                break;
            }
        }
        if (!alreadyRegistered) {
            targets.push_back(toName);
        }

        // Enregistrer la fonction de conversion dans une autre map
        // Ici on encapsule converterFunc dans un lambda générique prenant un SwAny et retournant un SwAny
        getConverters()[std::make_pair(fromName, toName)] = [converterFunc](const SwAny& any) -> SwAny {
            // On sait que any stocke un From
            From val = any.get<From>();
            To convertedVal = converterFunc(val);
            return SwAny::from(convertedVal);
        };
    }

    // Version avec std::string
    /**
     * @brief Returns whether the object reports convert.
     * @param targetName Value passed to the method.
     * @return `true` when the object reports convert; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool canConvert(const std::string& targetName) const {
        // Vérification du type exact
        if (typeNameStr == targetName) {
            return true;
        }
        // Vérification des règles de conversion
        std::lock_guard<std::mutex> lock(registryMutex());
        auto& rules = getConversionRules();
        auto it = rules.find(typeNameStr);
        if (it != rules.end()) {
            const auto& targets = it->second;
            for (auto& possibleTarget : targets) {
                if (possibleTarget == targetName) {
                    return true;
                }
            }
        }

        return false;
    }

    template<typename T>
    /**
     * @brief Returns the current type Name.
     * @return The current type Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static std::string getTypeName() {
        return std::string(typeid(T).name());
    }


    // Version template qui appelle la version string
    template<typename T>
    /**
     * @brief Returns whether the object reports convert.
     * @return `true` when the object reports convert; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool canConvert() const {
        auto targetName = getTypeName<T>();
        return canConvert(targetName);
    }


    // Version avec std::string pour la conversion
    /**
     * @brief Performs the `convert` operation.
     * @param targetName Value passed to the method.
     * @return The requested convert.
     */
    SwAny convert(const std::string& targetName) const {
        // Si le type actuel est déjà le bon
        if (typeNameStr == targetName) {
            return *this; // Pas besoin de convertir
        }

        // Vérifions si une règle de conversion existe
        std::function<SwAny(const SwAny&)> converter;
        {
            std::lock_guard<std::mutex> lock(registryMutex());
            auto& converters = getConverters();
            auto key = std::make_pair(typeNameStr, targetName);
            auto it = converters.find(key);
            if (it != converters.end()) {
                converter = it->second;
            }
        }

        if (converter) {
            return converter(*this);
        }

        swCError(kSwLogCategory_SwAny) << "No conversion rule registered from " << typeNameStr << " to " << targetName;
        return SwAny(); // Retourne un SwAny vide si impossible
    }

    // Version template qui appelle la version string
    template<typename T>
    /**
     * @brief Returns the current convert.
     * @return The current convert.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwAny convert() const {
        auto targetName = std::string(typeid(T).name());
        return convert(targetName);
    }



    // Instance : est-ce que *ce* SwAny est sérialisable ET désérialisable en string ?
    /**
     * @brief Returns whether the object reports serializable.
     * @return `true` when the object reports serializable; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isSerializable() const {
        if (typeNameStr.empty()) return false;
        return isSerializable(typeNameStr);
    }

    // Statique (par type T)
    template<typename T>
    /**
     * @brief Returns whether the object reports serializable.
     * @return The current serializable.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static bool isSerializable() {
        const std::string src = typeid(T).name();
        return isSerializable(src);
    }

    // Statique (par nom de type tel que stocké: typeid(T).name())
    /**
     * @brief Returns whether the object reports serializable.
     * @param src Value passed to the method.
     * @return The requested serializable.
     *
     * @details This query does not modify the object state.
     */
    static bool isSerializable(const std::string& src) {
        
        const std::string tSw    = typeid(SwString).name();
        const std::string tStd   = typeid(std::string).name();
        const std::string tBool  = typeid(bool).name();
        const std::string tInt   = typeid(int).name();
        const std::string tInt64 = typeid(long long).name();
        const std::string tStdInt64 = typeid(std::int64_t).name();
        const std::string tUInt64 = typeid(std::uint64_t).name();
        const std::string tULongLong = typeid(unsigned long long).name();
        const std::string tFloat = typeid(float).name();
        const std::string tDouble= typeid(double).name();
        const std::string tBytes = typeid(std::vector<uint8_t>).name();

        if (src == tSw || src == tStd || src == tBool || src == tInt ||
            src == tInt64 || src == tStdInt64 || src == tUInt64 || src == tULongLong ||
            src == tFloat || src == tDouble || src == tBytes)
            return true;

        std::lock_guard<std::mutex> lock(registryMutex());
        const auto& rules = getConversionRules();

        // 1) Vérifier qu'on peut faire src -> (SwString|std::string)
        bool hasToString = false;
        {
            auto it = rules.find(src);
            if (it != rules.end()) {
                for (const auto& target : it->second) {
                    if (target == tSw || target == tStd) { hasToString = true; break; }
                }
            }
        }

        if (!hasToString) return false;

        // 2) Vérifier qu'on peut faire (SwString|std::string) -> src
        bool hasFromString = false;
        {
            auto itSw  = rules.find(tSw);
            if (itSw != rules.end()) {
                for (const auto& target : itSw->second) {
                    if (target == src) { hasFromString = true; break; }
                }
            }
            if (!hasFromString) {
                auto itStd = rules.find(tStd);
                if (itStd != rules.end()) {
                    for (const auto& target : itStd->second) {
                        if (target == src) { hasFromString = true; break; }
                    }
                }
            }
        }

        return hasToString && hasFromString;
    }



    // Enregistre d'un coup la sérialisation et désérialisation string pour T.
    // toString : T -> SwString
    // fromString : SwString -> T
    template<typename T>
    /**
     * @brief Performs the `registerStringSerialization` operation.
     * @param toString Value passed to the method.
     * @param fromString Value passed to the method.
     * @return The requested register String Serialization.
     */
    static void registerStringSerialization(std::function<SwString(const T&)> toString,
                                    std::function<T(const SwString&)> fromString) {
        // T <-> SwString
        registerConversion<T, SwString>(toString);
        registerConversion<SwString, T>(fromString);

        // T <-> std::string (ponts pratiques)
        registerConversion<T, std::string>([toString](const T& v) {
            return toString(v).toStdString();
        });
        registerConversion<std::string, T>([fromString](const std::string& s) {
            return fromString(SwString(s));
        });
    }

    //Verifier si le metaType est dans le registery
    /**
     * @brief Returns whether the object reports my Type Registered.
     * @return `true` when the object reports my Type Registered; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isMyTypeRegistered() const {
        return !typeNameStr.empty() && isMetaTypeRegistered(typeNameStr);
    }


    // Vérifie par type T
    template<typename T>
    /**
     * @brief Returns whether the object reports meta Type Registered.
     * @return The current meta Type Registered.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static bool isMetaTypeRegistered() {
        const std::string tname = typeid(T).name();
        return isMetaTypeRegistered(tname);
    }

    // Vérifie par nom (tel que stocké: typeid(T).name())
    /**
     * @brief Returns whether the object reports meta Type Registered.
     * @param typeName Value passed to the method.
     * @return The requested meta Type Registered.
     *
     * @details This query does not modify the object state.
     */
    static bool isMetaTypeRegistered(const std::string& typeName) {
        // Un type est considéré comme "enregistré" s'il apparaît dans au moins
        // une des maps installées par registerMetaType<T>().
        const std::string tSw    = typeid(SwString).name();
        const std::string tStd   = typeid(std::string).name();
        const std::string tBool   = typeid(bool).name();
        const std::string tInt   = typeid(int).name();
        const std::string tInt64 = typeid(long long).name();
        const std::string tStdInt64 = typeid(std::int64_t).name();
        const std::string tUInt64 = typeid(std::uint64_t).name();
        const std::string tULongLong = typeid(unsigned long long).name();
        const std::string tFloat = typeid(float).name();
        const std::string tDouble= typeid(double).name();
        const std::string tBytes = typeid(std::vector<uint8_t>).name();

        // Trivial pour TOUS les types natifs du Storage (on garantit leurs conversions ci-dessous)
        if (typeName == tBool || typeName == tSw || typeName == tStd || typeName == tInt ||
            typeName == tInt64 || typeName == tStdInt64 || typeName == tUInt64 ||
            typeName == tULongLong ||
            typeName == tFloat || typeName == tDouble || typeName == tBytes)
            return true;

        std::lock_guard<std::mutex> lock(registryMutex());

        const auto& mData  = getDynamicDataMap();
        if (mData.find(typeName) != mData.end()) return true;

        const auto& mClear = getDynamicClearMap();
        if (mClear.find(typeName) != mClear.end()) return true;

        const auto& mCopy  = getDynamicCopyFromMap();
        if (mCopy.find(typeName) != mCopy.end()) return true;

        const auto& mFrom  = getDynamicFromVoidPtrMap();
        if (mFrom.find(typeName) != mFrom.end()) return true;

        const auto& mMove  = getDynamicMoveFromMap();
        if (mMove.find(typeName) != mMove.end()) return true;

        return false;
    }





    
    // Récupération du type sous forme de string
    /**
     * @brief Returns the current type Name.
     * @return The current type Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const std::string& typeName() const { return typeNameStr; }

    template <typename T>
    /**
     * @brief Returns the current register Meta Type.
     * @return The current register Meta Type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static void registerMetaType() {
        const std::string typeName = typeid(T).name();
        std::lock_guard<std::mutex> lock(registryMutex());

        // Déplacement dynamique
        getDynamicMoveFromMap()[typeName] = [typeName](SwAny& self, SwAny&& other) {
            if (self.storage.dynamic) {
                delete static_cast<T*>(self.storage.dynamic); // Nettoyer si nécessaire
            }
            self.storage.dynamic = other.storage.dynamic; // Déplacer les données
            other.storage.dynamic = nullptr;             // Vider l'ancien stockage
            self.typeNameStr = typeName;
            other.typeNameStr.clear();
        };

        // Clear dynamique
        getDynamicClearMap()[typeName] = [](SwAny& self) {
            delete static_cast<T*>(self.storage.dynamic);
            self.storage.dynamic = nullptr;
        };

        // CopyFrom dynamique
        getDynamicCopyFromMap()[typeName] = [](SwAny& self, const SwAny& other) {
            self.storage.dynamic = new T(*static_cast<const T*>(other.storage.dynamic));
            self.typeNameStr = other.typeNameStr;
        };

        // Data dynamique
        getDynamicDataMap()[typeName] = [](const SwAny& self) -> void* {
            return const_cast<void*>(reinterpret_cast<const void*>(self.storage.dynamic));
        };

        // FromVoidPtr dynamique
        getDynamicFromVoidPtrMap()[typeName] = [](void* ptr) -> SwAny {
            T *temp = static_cast<T*>(ptr);
            return SwAny::from(*temp);
        };
    }


    // Créer une instance depuis un type
    template <typename T>
    static void ensureDynamicMetaTypeRegistered() {
        static bool registered = []() {
            registerMetaType<T>();
            return true;
        }();
        (void)registered;
    }

    template <typename T>
    /**
     * @brief Performs the `from` operation.
     * @param value Value passed to the method.
     * @return The requested from.
     */
    static SwAny from(const T& value) {
        SwAny any;
        any.store(value);
        return any;
    }

    template <typename T>
    /**
     * @brief Performs the `fromValue` operation.
     * @param value Value passed to the method.
     * @return The requested from Value.
     */
    static SwAny fromValue(const T& value) {
        return from(value);
    }

    /**
     * @brief Performs the `fromVoidPtr` operation.
     * @param ptr Value passed to the method.
     * @param typeNameStr Value passed to the method.
     * @return The requested from Void Ptr.
     */
    static SwAny fromVoidPtr(void* ptr, const std::string& typeNameStr) {
        if (ptr == nullptr || typeNameStr.empty()) {
            swCError(kSwLogCategory_SwAny) << "Error: Null pointer or empty type name provided to fromVoidPtr.";
            return SwAny(); // Retourner une instance vide si le pointeur ou le type est invalide
        }
        // Gestion des types natifs et standard
        if (typeNameStr == typeid(bool).name()) {
            return SwAny(*static_cast<bool*>(ptr));
        } else if (typeNameStr == typeid(int).name()) {
            return SwAny(*static_cast<int*>(ptr));
        } else if (typeNameStr == typeid(long long).name()) {
            return SwAny(*static_cast<long long*>(ptr));
        } else if (!std::is_same<std::int64_t, long long>::value &&
                   typeNameStr == typeid(std::int64_t).name()) {
            return SwAny(*static_cast<std::int64_t*>(ptr));
        } else if (typeNameStr == typeid(float).name()) {
            return SwAny(*static_cast<float*>(ptr));
        } else if (typeNameStr == typeid(double).name()) {
            return SwAny(*static_cast<double*>(ptr));
        } else if (typeNameStr == typeid(uint32_t).name() ||
                   typeNameStr == typeid(unsigned int).name()) {
            return SwAny(*static_cast<uint32_t*>(ptr));
        } else if (typeNameStr == typeid(unsigned long long).name()) {
            return SwAny(*static_cast<unsigned long long*>(ptr));
        } else if (!std::is_same<std::uint64_t, unsigned long long>::value &&
                   typeNameStr == typeid(std::uint64_t).name()) {
            return SwAny(*static_cast<std::uint64_t*>(ptr));
        } else if (typeNameStr == typeid(std::string).name()) {
            return SwAny(*static_cast<std::string*>(ptr));
        } else if (typeNameStr == typeid(std::vector<uint8_t>).name()) {
            return SwAny(*static_cast<std::vector<uint8_t>*>(ptr));
        }

        // Gestion des types dynamiques
        std::function<SwAny(void*)> fromVoidPtr;
        {
            std::lock_guard<std::mutex> lock(registryMutex());
            auto& dynamicMap = getDynamicFromVoidPtrMap();
            auto it = dynamicMap.find(typeNameStr);
            if (it != dynamicMap.end()) {
                fromVoidPtr = it->second;
            }
        }
        if (fromVoidPtr) {
            return fromVoidPtr(ptr); // Appel de la fonction dynamique
        }

        // Si le type n'est pas trouvé, afficher un message clair
        swCError(kSwLogCategory_SwAny) << "Error: Type '" << typeNameStr << "' not found in dynamic type map.";
        swCError(kSwLogCategory_SwAny) << "Available types in the map:";

        {
            std::lock_guard<std::mutex> lock(registryMutex());
            for (const auto& entry : getDynamicFromVoidPtrMap()) {
                swCError(kSwLogCategory_SwAny) << "  - " << entry.first; // Afficher tous les types enregistrés
            }
        }
        return SwAny();
    }



    // Récupérer la valeur avec cast explicite
    template <typename T>
    /**
     * @brief Returns the current get.
     * @return The current get.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const T& get() const {
        if (typeNameStr != typeid(T).name()) {
            throw std::runtime_error("Type mismatch in SwAny::get");
        }
        const void* ptr = data();
        if (!ptr) {
            throw std::runtime_error("Null storage in SwAny::get");
        }
        return *reinterpret_cast<const T*>(ptr);
    }

    template <typename T>
    /**
     * @brief Returns the current get.
     * @return The current get.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    T& get() {
        if (typeNameStr != typeid(T).name()) {
            throw std::runtime_error("Type mismatch in SwAny::get");
        }
        void* ptr = data();
        if (!ptr) {
            throw std::runtime_error("Null storage in SwAny::get");
        }
        return *reinterpret_cast<T*>(ptr);
    }


    // Méthode pour obtenir un pointeur générique vers les données
    /**
     * @brief Returns the current data.
     * @return The current data.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    void* data() const {
        // Gestion des types natifs
        if (typeNameStr == typeid(bool).name()) {
            return const_cast<void*>(static_cast<const void*>(&storage.b));
        } else if (typeNameStr == typeid(int).name()) {
            return const_cast<void*>(static_cast<const void*>(&storage.i));
        } else if (typeNameStr == typeid(long long).name()) {
            return const_cast<void*>(static_cast<const void*>(&storage.i64));
        } else if (!std::is_same<std::int64_t, long long>::value &&
                   typeNameStr == typeid(std::int64_t).name()) {
            return const_cast<void*>(static_cast<const void*>(&storage.stdI64));
        } else if (typeNameStr == typeid(float).name()) {
            return const_cast<void*>(static_cast<const void*>(&storage.f));
        } else if (typeNameStr == typeid(double).name()) {
            return const_cast<void*>(static_cast<const void*>(&storage.d));
        } else if (typeNameStr == typeid(uint32_t).name() ||
                   typeNameStr == typeid(unsigned int).name()) {
            return const_cast<void*>(static_cast<const void*>(&storage.u32));
        } else if (typeNameStr == typeid(unsigned long long).name()) {
            return const_cast<void*>(static_cast<const void*>(&storage.u64));
        } else if (!std::is_same<std::uint64_t, unsigned long long>::value &&
                   typeNameStr == typeid(std::uint64_t).name()) {
            return const_cast<void*>(static_cast<const void*>(&storage.stdU64));
        } else if (typeNameStr == typeid(std::string).name()) {
            return const_cast<void*>(static_cast<const void*>(&storage.str));
        } else if (typeNameStr == typeid(std::vector<uint8_t>).name()) {
            return const_cast<void*>(static_cast<const void*>(&storage.byteArray));
        }

        // Gestion des types dynamiques
        std::function<void*(const SwAny&)> dataGetter;
        {
            std::lock_guard<std::mutex> lock(registryMutex());
            auto& dynamicDataMap = getDynamicDataMap();
            auto it = dynamicDataMap.find(typeNameStr);
            if (it != dynamicDataMap.end()) {
                dataGetter = it->second;
            }
        }
        return dataGetter ? dataGetter(*this) : nullptr;
    }




    std::string typeNameStr;  // Sauvegarde du nom du type

    /**
     * @brief Performs the `copyFrom` operation.
     * @param other Value passed to the method.
     */
    void copyFrom(const SwAny& other) {
        // Repartir d'un stockage vide avant de recopier la valeur.
        clear();
        const std::string otherTypeName = other.typeNameStr;

        if (otherTypeName.empty()) {
            return; // Aucun type à copier
        }

        // Gestion des types natifs
        if (otherTypeName == typeid(bool).name()) {
            storage.b = other.storage.b;
            typeNameStr = otherTypeName;
        } else if (otherTypeName == typeid(int).name()) {
            storage.i = other.storage.i;
            typeNameStr = otherTypeName;
        } else if (otherTypeName == typeid(long long).name()) {
            storage.i64 = other.storage.i64;
            typeNameStr = otherTypeName;
        } else if (!std::is_same<std::int64_t, long long>::value &&
                   otherTypeName == typeid(std::int64_t).name()) {
            storage.stdI64 = other.storage.stdI64;
            typeNameStr = otherTypeName;
        } else if (otherTypeName == typeid(float).name()) {
            storage.f = other.storage.f;
            typeNameStr = otherTypeName;
        } else if (otherTypeName == typeid(double).name()) {
            storage.d = other.storage.d;
            typeNameStr = otherTypeName;
        } else if (otherTypeName == typeid(uint32_t).name() ||
                   otherTypeName == typeid(unsigned int).name()) {
            storage.u32 = other.storage.u32;
            typeNameStr = otherTypeName;
        } else if (otherTypeName == typeid(unsigned long long).name()) {
            storage.u64 = other.storage.u64;
            typeNameStr = otherTypeName;
        } else if (!std::is_same<std::uint64_t, unsigned long long>::value &&
                   otherTypeName == typeid(std::uint64_t).name()) {
            storage.stdU64 = other.storage.stdU64;
            typeNameStr = otherTypeName;
        } else if (otherTypeName == typeid(std::string).name()) {
            new (&storage.str) std::string(other.storage.str);
            typeNameStr = otherTypeName;
        } else if (otherTypeName == typeid(std::vector<uint8_t>).name()) {
            new (&storage.byteArray) std::vector<uint8_t>(other.storage.byteArray);
            typeNameStr = otherTypeName;
        }
        // Gestion des types dynamiques
        else {
            std::function<void(SwAny&, const SwAny&)> copyFrom;
            {
                std::lock_guard<std::mutex> lock(registryMutex());
                auto& dynamicCopyMap = getDynamicCopyFromMap();
                auto it = dynamicCopyMap.find(otherTypeName);
                if (it != dynamicCopyMap.end()) {
                    copyFrom = it->second;
                }
            }
            if (copyFrom) {
                copyFrom(*this, other); // Appel de la fonction dynamique pour copier
            }
            // Sans règle enregistrée, copier le pointeur dynamique serait dangereux.
            else if (other.storage.dynamic) {
                swCError(kSwLogCategory_SwAny) << "Cannot copy unregistered dynamic SwAny type: " << otherTypeName;
            }
        }
    }


    // Méthode pour déplacer les données (move)
    /**
     * @brief Performs the `moveFrom` operation.
     * @param other Value passed to the method.
     */
    void moveFrom(SwAny&& other) {
        // Déplacer le type du nom
        const std::string otherTypeName = other.typeNameStr;
        typeNameStr = otherTypeName;

        if (typeNameStr.empty()) {
            other.clear();
            return;
        }

        // Cas spécifiques pour les types natifs ou gérés explicitement
        if (typeNameStr == typeid(bool).name()) {
            storage.b = other.storage.b;
        } else if (typeNameStr == typeid(int).name()) {
            storage.i = other.storage.i;
        } else if (typeNameStr == typeid(long long).name()) {
            storage.i64 = other.storage.i64;
        } else if (!std::is_same<std::int64_t, long long>::value &&
                   typeNameStr == typeid(std::int64_t).name()) {
            storage.stdI64 = other.storage.stdI64;
        } else if (typeNameStr == typeid(uint32_t).name() ||
                   typeNameStr == typeid(unsigned int).name()) {
            storage.u32 = other.storage.u32;
        } else if (typeNameStr == typeid(unsigned long long).name()) {
            storage.u64 = other.storage.u64;
        } else if (!std::is_same<std::uint64_t, unsigned long long>::value &&
                   typeNameStr == typeid(std::uint64_t).name()) {
            storage.stdU64 = other.storage.stdU64;
        } else if (typeNameStr == typeid(float).name()) {
            storage.f = other.storage.f;
        } else if (typeNameStr == typeid(double).name()) {
            storage.d = other.storage.d;
        } else if (typeNameStr == typeid(std::string).name()) {
            new (&storage.str) std::string(std::move(other.storage.str));
        } else if (typeNameStr == typeid(std::vector<uint8_t>).name()) {
            new (&storage.byteArray) std::vector<uint8_t>(std::move(other.storage.byteArray));
        }
        // Gestion des types dynamiques enregistrés
        else {
            std::function<void(SwAny&, SwAny&&)> moveFrom;
            {
                std::lock_guard<std::mutex> lock(registryMutex());
                auto& dynamicMoveMap = getDynamicMoveFromMap();
                auto it = dynamicMoveMap.find(typeNameStr);
                if (it != dynamicMoveMap.end()) {
                    moveFrom = it->second;
                }
            }
            if (moveFrom) {
                moveFrom(*this, std::move(other));
            }
            // Sans règle enregistrée, déplacer le pointeur dynamique serait dangereux.
            else if (other.storage.dynamic) {
                swCError(kSwLogCategory_SwAny) << "Cannot move unregistered dynamic SwAny type: " << typeNameStr;
                typeNameStr.clear();
                storage.dynamic = nullptr;
            }
        }

        // Réinitialisation de l'objet source
        other.clear();
    }





    // Méthodes pour stocker les données
    template <typename T>
    /**
     * @brief Performs the `store` operation.
     * @param value Value passed to the method.
     */
    typename std::enable_if<
        !(std::is_same<T, std::int64_t>::value &&
          !std::is_same<std::int64_t, long long>::value) &&
        !(std::is_same<T, std::uint64_t>::value &&
          !std::is_same<std::uint64_t, unsigned long long>::value),
        void>::type store(const T& value) {
        ensureDynamicMetaTypeRegistered<T>();
        T* copy = new T(value);
        clear();
        storage.dynamic = copy;
        typeNameStr = typeid(T).name();
    }
    template <typename T>
    typename std::enable_if<std::is_same<T, std::int64_t>::value &&
                            !std::is_same<std::int64_t, long long>::value,
                            void>::type store(T val) {
        clear();
        storage.stdI64 = val;
        typeNameStr = typeid(std::int64_t).name();
    }
    template <typename T>
    typename std::enable_if<std::is_same<T, std::uint64_t>::value &&
                            !std::is_same<std::uint64_t, unsigned long long>::value,
                            void>::type store(T val) {
        clear();
        storage.stdU64 = val;
        typeNameStr = typeid(std::uint64_t).name();
    }
    /**
     * @brief Performs the `store` operation.
     * @param b Value passed to the method.
     */
    void store(bool val) { clear(); storage.b = val; typeNameStr = typeid(bool).name(); }
    /**
     * @brief Performs the `store` operation.
     * @param i Value passed to the method.
     */
    void store(int val) { clear(); storage.i = val; typeNameStr = typeid(int).name(); }
    /**
     * @brief Performs the `store` operation.
     * @param i64 Value passed to the method.
     */
    void store(long long val) { clear(); storage.i64 = val; typeNameStr = typeid(long long).name(); }
    /**
     * @brief Performs the `store` operation.
     * @param u64 Value passed to the method.
     */
    void store(unsigned long long val) { clear(); storage.u64 = val; typeNameStr = typeid(unsigned long long).name(); }
    /**
     * @brief Performs the `store` operation.
     * @param f Value passed to the method.
     */
    void store(float val) { clear(); storage.f = val; typeNameStr = typeid(float).name(); }
    /**
     * @brief Performs the `store` operation.
     * @param d Value passed to the method.
     */
    void store(double val) { clear(); storage.d = val; typeNameStr = typeid(double).name(); }
    /**
     * @brief Performs the `store` operation.
     * @param typeNameStr Value passed to the method.
     */
    void store(const std::string& val) { clear(); new (&storage.str) std::string(val); typeNameStr = typeid(std::string).name(); }
    /**
     * @brief Performs the `store` operation.
     * @param typeNameStr Value passed to the method.
     */
    void store(const std::vector<uint8_t>& val) { clear(); new (&storage.byteArray) std::vector<uint8_t>(val); typeNameStr = typeid(std::vector<uint8_t>).name(); }
    /**
     * @brief Performs the `store` operation.
     * @param dynamic Value passed to the method.
     */
    void store(const SwString& val) { clear(); storage.dynamic = new SwString(val); typeNameStr = typeid(SwString).name(); }
    /**
     * @brief Performs the `store` operation.
     */
    void store(const SwByteArray& val) { store(SwString(val)); }
    /**
     * @brief Performs the `store` operation.
     * @param u32 Value passed to the method.
     */
    void store(uint32_t val) { clear(); storage.u32 = val; typeNameStr = typeid(uint32_t).name(); }

    // Libérer les ressources allouées
    /**
     * @brief Clears the current object state.
     */
    void clear() {
        if (!typeNameStr.empty()) {
            // Utilisation de _dynamicClear si une fonction correspondante existe pour ce type
            std::function<void(SwAny&)> clearFunc;
            {
                std::lock_guard<std::mutex> lock(registryMutex());
                auto& dynamicClearMap = getDynamicClearMap(); // Accès à la map _dynamicClear
                auto it = dynamicClearMap.find(typeNameStr);
                if (it != dynamicClearMap.end()) {
                    clearFunc = it->second;
                }
            }
            if (clearFunc) {
                clearFunc(*this); // Appel de la fonction de destruction dynamique
            } else if (typeNameStr == typeid(std::string).name()) {
                storage.str.~basic_string();
            } else if (typeNameStr == typeid(std::vector<uint8_t>).name()) {
                storage.byteArray.~vector();
            }
        }

        // Réinitialisation des données
        storage.dynamic = nullptr;
        typeNameStr.clear();
    }





    /**
     * @brief Performs the `function<void` operation.
     * @return The requested function<void.
     */
    static std::map<std::string, std::function<void(SwAny&)>>& getDynamicClearMap() {
        static std::map<std::string, std::function<void(SwAny&)>> _dynamicClear;
        return _dynamicClear;
    }

    // Méthode pour accéder à _dynamicCopyFrom
    /**
     * @brief Performs the `function<void` operation.
     * @return The requested function<void.
     */
    static std::map<std::string, std::function<void(SwAny&, const SwAny&)>>& getDynamicCopyFromMap() {
        static std::map<std::string, std::function<void(SwAny&, const SwAny&)>> _dynamicCopyFrom;
        return _dynamicCopyFrom;
    }

    // Méthode pour accéder à _dynamicData
    /**
     * @brief Performs the `function<void*` operation.
     * @return The requested function<void*.
     */
    static std::map<std::string, std::function<void*(const SwAny&)>>& getDynamicDataMap() {
        static std::map<std::string, std::function<void*(const SwAny&)>> _dynamicData;
        return _dynamicData;
    }

    // Méthode pour accéder à _dynamicFromVoidPtr
    /**
     * @brief Performs the `function<SwAny` operation.
     * @return The requested function<Sw Any.
     */
    static std::map<std::string, std::function<SwAny(void*)>>& getDynamicFromVoidPtrMap() {
        static std::map<std::string, std::function<SwAny(void*)>> _dynamicFromVoidPtr;
        return _dynamicFromVoidPtr;
    }

    // Méthode pour accéder à _dynamicMoveFrom
    /**
     * @brief Performs the `function<void` operation.
     * @return The requested function<void.
     */
    static std::map<std::string, std::function<void(SwAny&, SwAny&&)>>& getDynamicMoveFromMap() {
        static std::map<std::string, std::function<void(SwAny&, SwAny&&)>> _dynamicMoveFrom;
        return _dynamicMoveFrom;
    }

    // Map statique : nom du type source -> liste de noms de types cibles
    /**
     * @brief Returns the current conversion Rules.
     * @return The current conversion Rules.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static std::map<std::string, std::vector<std::string>>& getConversionRules() {
        static std::map<std::string, std::vector<std::string>> conversionRules;
        return conversionRules;
    }

    // Map statique pour stocker les fonctions de conversion
    // Clé : (fromType, toType)
    /**
     * @brief Performs the `function<SwAny` operation.
     * @return The requested function<Sw Any.
     */
    static std::map<std::pair<std::string, std::string>, std::function<SwAny(const SwAny&)>>& getConverters() {
        static std::map<std::pair<std::string, std::string>, std::function<SwAny(const SwAny&)>> converters;
        return converters;
    }

    static std::mutex& registryMutex() {
        static std::mutex mutex;
        return mutex;
    }


public:

    /**
     * @brief Returns the current to Bool.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool toBool() const {
        if (typeNameStr == typeid(bool).name()) {
            return get<bool>();
        } else if (typeNameStr == typeid(int).name()) {
            return get<int>() != 0;
        } else if (typeNameStr == typeid(long long).name()) {
            return get<long long>() != 0;
        } else if (!std::is_same<std::int64_t, long long>::value &&
                   typeNameStr == typeid(std::int64_t).name()) {
            return get<std::int64_t>() != 0;
        } else if (typeNameStr == typeid(unsigned int).name()) {
            return get<unsigned int>() != 0;
        } else if (typeNameStr == typeid(uint32_t).name()) {
            return get<uint32_t>() != 0u;
        } else if (typeNameStr == typeid(unsigned long long).name()) {
            return get<unsigned long long>() != 0ull;
        } else if (!std::is_same<std::uint64_t, unsigned long long>::value &&
                   typeNameStr == typeid(std::uint64_t).name()) {
            return get<std::uint64_t>() != 0ull;
        } else if (typeNameStr == typeid(double).name()) {
            return std::fabs(get<double>()) > std::numeric_limits<double>::epsilon();
        } else if (typeNameStr == typeid(float).name()) {
            return std::fabs(get<float>()) > std::numeric_limits<float>::epsilon();
        } else if (typeNameStr == typeid(SwString).name()) {
            SwString s = get<SwString>().trimmed().toLower();
            return (s == "true" || s == "1" || s == "yes" || s == "on");
        } else if (typeNameStr == typeid(std::string).name()) {
            SwString s(get<std::string>());
            s = s.trimmed().toLower();
            return (s == "true" || s == "1" || s == "yes" || s == "on");
        } else if (canConvert<bool>()) {
            SwAny converted = convert<bool>();
            return converted.get<bool>();
        }
        // swCError(kSwLogCategory_SwAny) << "Error: Not convertible to bool. Current type: " << typeNameStr;
        return false;
    }

    /**
 * @brief Convertit la valeur stockée dans SwAny en un entier.
 *
 * Si le type interne n'est pas un entier mais peut être converti en entier
 * (via une règle de conversion préalablement enregistrée), la conversion est tentée.
 *
 * @return int La valeur convertie si possible, sinon retourne 0 avec un message d'erreur dans swCError(kSwLogCategory_SwAny).
 */
    int toInt(bool *ok = nullptr) const {
        if (typeNameStr == typeid(int).name()) {
            if (ok) *ok = true;
            return get<int>();
        } else if (typeNameStr == typeid(long long).name()) {
            const long long value = get<long long>();
            if (value < static_cast<long long>(std::numeric_limits<int>::min()) ||
                value > static_cast<long long>(std::numeric_limits<int>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<int>(value);
        } else if (!std::is_same<std::int64_t, long long>::value &&
                   typeNameStr == typeid(std::int64_t).name()) {
            const std::int64_t value = get<std::int64_t>();
            if (value < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
                value > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<int>(value);
        } else if (typeNameStr == typeid(unsigned int).name()) {
            const unsigned int value = get<unsigned int>();
            if (value > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<int>(value);
        } else if (typeNameStr == typeid(uint32_t).name()) {
            const uint32_t value = get<uint32_t>();
            if (value > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<int>(value);
        } else if (typeNameStr == typeid(unsigned long long).name()) {
            const unsigned long long value = get<unsigned long long>();
            if (value > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<int>(value);
        } else if (!std::is_same<std::uint64_t, unsigned long long>::value &&
                   typeNameStr == typeid(std::uint64_t).name()) {
            const std::uint64_t value = get<std::uint64_t>();
            if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<int>(value);
        } else if (typeNameStr == typeid(bool).name()) {
            if (ok) *ok = true;
            return get<bool>() ? 1 : 0;
        } else if (typeNameStr == typeid(float).name()) {
            const float value = get<float>();
            if (!std::isfinite(value) ||
                value < static_cast<float>(std::numeric_limits<int>::min()) ||
                value > static_cast<float>(std::numeric_limits<int>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<int>(value);
        } else if (typeNameStr == typeid(double).name()) {
            const double value = get<double>();
            if (!std::isfinite(value) ||
                value < static_cast<double>(std::numeric_limits<int>::min()) ||
                value > static_cast<double>(std::numeric_limits<int>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<int>(value);
        } else if (typeNameStr == typeid(SwString).name()) {
            return get<SwString>().toInt(ok);
        } else if (typeNameStr == typeid(std::string).name()) {
            return SwString(get<std::string>()).toInt(ok);
        } else if (canConvert<int>()) {
            SwAny converted = convert<int>();
            if (ok) *ok = true;
            return converted.get<int>();
        } else {
            // swCError(kSwLogCategory_SwAny) << "Error: Not convertible to int. Current type: " << typeNameStr;
            if (ok) *ok = false;
            return 0;
        }
    }

    /**
     * @brief Performs the `toLongLong` operation.
     * @param ok Optional flag updated to report success.
     * @return The requested to Long Long.
     */
    long long toLongLong(bool* ok = nullptr) const {
        if (typeNameStr == typeid(long long).name()) {
            if (ok) *ok = true;
            return get<long long>();
        } else if (!std::is_same<std::int64_t, long long>::value &&
                   typeNameStr == typeid(std::int64_t).name()) {
            if (ok) *ok = true;
            return static_cast<long long>(get<std::int64_t>());
        } else if (typeNameStr == typeid(int).name()) {
            if (ok) *ok = true;
            return static_cast<long long>(get<int>());
        } else if (typeNameStr == typeid(unsigned int).name()) {
            if (ok) *ok = true;
            return static_cast<long long>(get<unsigned int>());
        } else if (typeNameStr == typeid(uint32_t).name()) {
            if (ok) *ok = true;
            return static_cast<long long>(get<uint32_t>());
        } else if (typeNameStr == typeid(unsigned long long).name()) {
            const unsigned long long value = get<unsigned long long>();
            if (value > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<long long>(value);
        } else if (!std::is_same<std::uint64_t, unsigned long long>::value &&
                   typeNameStr == typeid(std::uint64_t).name()) {
            const std::uint64_t value = get<std::uint64_t>();
            if (value > static_cast<std::uint64_t>(std::numeric_limits<long long>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<long long>(value);
        } else if (typeNameStr == typeid(bool).name()) {
            if (ok) *ok = true;
            return get<bool>() ? 1ll : 0ll;
        } else if (typeNameStr == typeid(float).name()) {
            const float value = get<float>();
            if (!std::isfinite(value) ||
                value < static_cast<float>(std::numeric_limits<long long>::min()) ||
                value > static_cast<float>(std::numeric_limits<long long>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<long long>(value);
        } else if (typeNameStr == typeid(double).name()) {
            const double value = get<double>();
            if (!std::isfinite(value) ||
                value < static_cast<double>(std::numeric_limits<long long>::min()) ||
                value > static_cast<double>(std::numeric_limits<long long>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<long long>(value);
        } else if (typeNameStr == typeid(SwString).name()) {
            return get<SwString>().toLongLong(ok);
        } else if (typeNameStr == typeid(std::string).name()) {
            return SwString(get<std::string>()).toLongLong(ok);
        } else if (canConvert<long long>()) {
            SwAny converted = convert<long long>();
            if (ok) *ok = true;
            return converted.get<long long>();
        }
        if (ok) *ok = false;
        return 0;
    }

    /**
     * @brief Performs the `toInt64` operation.
     * @param ok Optional flag updated to report success.
     * @return The requested signed 64-bit integer.
     */
    std::int64_t toInt64(bool* ok = nullptr) const {
        return static_cast<std::int64_t>(toLongLong(ok));
    }

    /**
     * @brief Performs the `toUInt64` operation.
     * @param ok Optional flag updated to report success.
     * @return The requested unsigned 64-bit integer.
     */
    std::uint64_t toUInt64(bool* ok = nullptr) const {
        if (typeNameStr.empty()) {
            if (ok) *ok = false;
            return 0;
        }
        if (typeNameStr == typeid(unsigned long long).name()) {
            if (ok) *ok = true;
            return static_cast<std::uint64_t>(get<unsigned long long>());
        }
        if (!std::is_same<std::uint64_t, unsigned long long>::value &&
            typeNameStr == typeid(std::uint64_t).name()) {
            if (ok) *ok = true;
            return get<std::uint64_t>();
        }
        if (typeNameStr == typeid(uint32_t).name()) {
            if (ok) *ok = true;
            return static_cast<std::uint64_t>(get<uint32_t>());
        }
        if (typeNameStr == typeid(unsigned int).name()) {
            if (ok) *ok = true;
            return static_cast<std::uint64_t>(get<unsigned int>());
        }
        if (typeNameStr == typeid(int).name()) {
            const int value = get<int>();
            if (value < 0) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<std::uint64_t>(value);
        }
        if (typeNameStr == typeid(long long).name()) {
            const long long value = get<long long>();
            if (value < 0) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<std::uint64_t>(value);
        }
        if (!std::is_same<std::int64_t, long long>::value &&
            typeNameStr == typeid(std::int64_t).name()) {
            const std::int64_t value = get<std::int64_t>();
            if (value < 0) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<std::uint64_t>(value);
        }
        if (typeNameStr == typeid(bool).name()) {
            if (ok) *ok = true;
            return get<bool>() ? 1ull : 0ull;
        }
        if (typeNameStr == typeid(float).name()) {
            const float value = get<float>();
            const long double wide = static_cast<long double>(value);
            if (!std::isfinite(value) || wide < 0.0L ||
                wide > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<std::uint64_t>(value);
        }
        if (typeNameStr == typeid(double).name()) {
            const double value = get<double>();
            const long double wide = static_cast<long double>(value);
            if (!std::isfinite(value) || wide < 0.0L ||
                wide > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return static_cast<std::uint64_t>(value);
        }
        if (typeNameStr == typeid(SwString).name()) {
            return parseUInt64_(get<SwString>(), ok);
        }
        if (typeNameStr == typeid(std::string).name()) {
            return parseUInt64_(SwString(get<std::string>()), ok);
        }
        if (canConvert<std::uint64_t>()) {
            SwAny converted = convert<std::uint64_t>();
            if (ok) *ok = true;
            return converted.get<std::uint64_t>();
        }
        if (ok) *ok = false;
        return 0;
    }

    /**
     * @brief Performs the `toULongLong` operation.
     * @param ok Optional flag updated to report success.
     * @return The requested unsigned long long integer.
     */
    unsigned long long toULongLong(bool* ok = nullptr) const {
        return static_cast<unsigned long long>(toUInt64(ok));
    }

    /**
 * @brief Convertit la valeur stockée dans SwAny en un float.
 *
 * Si le type interne n'est pas un float mais peut être converti en float,
 * la conversion est tentée.
 *
 * @return float La valeur convertie si possible, sinon retourne 0.0f avec un message d'erreur dans swCError(kSwLogCategory_SwAny).
 */
    float toFloat() const {
        if (typeNameStr.empty()) {
            return 0.0f;
        }
        if (typeNameStr == typeid(float).name()) {
            return get<float>();
        } else if (typeNameStr == typeid(double).name()) {
            const double value = get<double>();
            if (!std::isfinite(value) ||
                value < -static_cast<double>(std::numeric_limits<float>::max()) ||
                value > static_cast<double>(std::numeric_limits<float>::max())) {
                return 0.0f;
            }
            return static_cast<float>(value);
        } else if (typeNameStr == typeid(bool).name()) {
            return get<bool>() ? 1.0f : 0.0f;
        } else if (typeNameStr == typeid(int).name()) {
            return static_cast<float>(get<int>());
        } else if (typeNameStr == typeid(long long).name()) {
            return static_cast<float>(get<long long>());
        } else if (!std::is_same<std::int64_t, long long>::value &&
                   typeNameStr == typeid(std::int64_t).name()) {
            return static_cast<float>(get<std::int64_t>());
        } else if (typeNameStr == typeid(unsigned int).name()) {
            return static_cast<float>(get<unsigned int>());
        } else if (typeNameStr == typeid(uint32_t).name()) {
            return static_cast<float>(get<uint32_t>());
        } else if (typeNameStr == typeid(unsigned long long).name()) {
            return static_cast<float>(get<unsigned long long>());
        } else if (!std::is_same<std::uint64_t, unsigned long long>::value &&
                   typeNameStr == typeid(std::uint64_t).name()) {
            return static_cast<float>(get<std::uint64_t>());
        } else if (typeNameStr == typeid(SwString).name()) {
            bool ok = false;
            const float value = get<SwString>().toFloat(&ok);
            if (ok) return value;
        } else if (typeNameStr == typeid(std::string).name()) {
            bool ok = false;
            const float value = SwString(get<std::string>()).toFloat(&ok);
            if (ok) return value;
        } else if (canConvert<float>()) {
            SwAny converted = convert<float>();
            return converted.get<float>();
        }
        // swCError(kSwLogCategory_SwAny) << "Error: Not convertible to float. Current type: " << typeNameStr;
        return 0.0f;
    }

    /**
     * @brief Convertit la valeur stockée dans SwAny en un double.
     *
     * Si le type interne n'est pas un double mais peut être converti en double,
     * la conversion est tentée.
     *
     * @return double La valeur convertie si possible, sinon retourne 0.0 avec un message d'erreur dans swCError(kSwLogCategory_SwAny).
     */
    double toDouble() const {
        if (typeNameStr.empty()) {
            return 0.0;
        }
        if (typeNameStr == typeid(double).name()) {
            return get<double>();
        }
        if (typeNameStr == typeid(float).name()) {
            return static_cast<double>(get<float>());
        }
        if (typeNameStr == typeid(bool).name()) {
            return get<bool>() ? 1.0 : 0.0;
        }
        if (typeNameStr == typeid(int).name()) {
            return static_cast<double>(get<int>());
        }
        if (typeNameStr == typeid(long long).name()) {
            return static_cast<double>(get<long long>());
        }
        if (!std::is_same<std::int64_t, long long>::value &&
            typeNameStr == typeid(std::int64_t).name()) {
            return static_cast<double>(get<std::int64_t>());
        }
        if (typeNameStr == typeid(unsigned int).name()) {
            return static_cast<double>(get<unsigned int>());
        }
        if (typeNameStr == typeid(uint32_t).name()) {
            return static_cast<double>(get<uint32_t>());
        }
        if (typeNameStr == typeid(unsigned long long).name()) {
            return static_cast<double>(get<unsigned long long>());
        }
        if (!std::is_same<std::uint64_t, unsigned long long>::value &&
            typeNameStr == typeid(std::uint64_t).name()) {
            return static_cast<double>(get<std::uint64_t>());
        }
        if (typeNameStr == typeid(SwString).name()) {
            bool ok=false;
            double val = get<SwString>().toDouble(&ok);
            if (ok) return val;
        }
        if (typeNameStr == typeid(std::string).name()) {
            try {
                return std::stod(get<std::string>());
            } catch (...) {
            }
        }
        if (canConvert<double>()) {
            SwAny converted = convert<double>();
            return converted.get<double>();
        }

        // swCError(kSwLogCategory_SwAny) << "Error: Not convertible to double. Current type: " << typeNameStr;
        return 0.0;
    }

    /**
     * @brief Returns the current to UInt.
     * @return The current to UInt.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    uint32_t toUInt() const {
        if (typeNameStr.empty()) {
            return 0u;
        }
        if (typeNameStr == typeid(uint32_t).name()) {
            return get<uint32_t>();
        } else if (typeNameStr == typeid(unsigned int).name()) {
            return static_cast<uint32_t>(get<unsigned int>());
        } else if (typeNameStr == typeid(int).name()) {
            const int v = get<int>();
            return v < 0 ? 0u : static_cast<uint32_t>(v);
        } else if (typeNameStr == typeid(long long).name()) {
            const long long v = get<long long>();
            if (v < 0) return 0u;
            if (v > static_cast<long long>(std::numeric_limits<uint32_t>::max())) {
                return std::numeric_limits<uint32_t>::max();
            }
            return static_cast<uint32_t>(v);
        } else if (!std::is_same<std::int64_t, long long>::value &&
                   typeNameStr == typeid(std::int64_t).name()) {
            const std::int64_t v = get<std::int64_t>();
            if (v < 0) return 0u;
            if (v > static_cast<std::int64_t>(std::numeric_limits<uint32_t>::max())) {
                return std::numeric_limits<uint32_t>::max();
            }
            return static_cast<uint32_t>(v);
        } else if (typeNameStr == typeid(unsigned long long).name()) {
            const unsigned long long v = get<unsigned long long>();
            if (v > static_cast<unsigned long long>(std::numeric_limits<uint32_t>::max())) {
                return std::numeric_limits<uint32_t>::max();
            }
            return static_cast<uint32_t>(v);
        } else if (!std::is_same<std::uint64_t, unsigned long long>::value &&
                   typeNameStr == typeid(std::uint64_t).name()) {
            const std::uint64_t v = get<std::uint64_t>();
            if (v > static_cast<std::uint64_t>(std::numeric_limits<uint32_t>::max())) {
                return std::numeric_limits<uint32_t>::max();
            }
            return static_cast<uint32_t>(v);
        } else if (typeNameStr == typeid(double).name()) {
            const double v = get<double>();
            if (!std::isfinite(v) || v < 0.0) return 0u;
            if (v > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
                return std::numeric_limits<uint32_t>::max();
            }
            return static_cast<uint32_t>(v);
        } else if (typeNameStr == typeid(float).name()) {
            const float v = get<float>();
            if (!std::isfinite(v) || v < 0.f) return 0u;
            if (v > static_cast<float>(std::numeric_limits<uint32_t>::max())) {
                return std::numeric_limits<uint32_t>::max();
            }
            return static_cast<uint32_t>(v);
        } else if (typeNameStr == typeid(bool).name()) {
            return get<bool>() ? 1u : 0u;
        } else if (typeNameStr == typeid(SwString).name()) {
            try {
                return static_cast<uint32_t>(std::stoul(get<SwString>().toStdString(), nullptr, 0));
            } catch (...) {
            }
        } else if (typeNameStr == typeid(std::string).name()) {
            try {
                return static_cast<uint32_t>(std::stoul(get<std::string>(), nullptr, 0));
            } catch (...) {
            }
        } else if (canConvert<uint32_t>()) {
            SwAny converted = convert<uint32_t>();
            return converted.get<uint32_t>();
        }
        // swCError(kSwLogCategory_SwAny) << "Error: Not convertible to uint32_t. Current type: " << typeNameStr;
        return 0u;
    }

    /**
     * @brief Convertit la valeur stockée dans SwAny en un tableau d'octets.
     *
     * Si le type interne n'est pas std::vector<uint8_t> mais peut être converti vers ce type,
     * la conversion est tentée.
     *
     * @return std::vector<uint8_t> La valeur convertie si possible, sinon un tableau vide avec message d'erreur.
     */
    std::vector<uint8_t> toByteArray() const {
        if (typeNameStr == typeid(std::vector<uint8_t>).name()) {
            return get<std::vector<uint8_t>>();
        } else if (canConvert<std::vector<uint8_t>>()) {
            SwAny converted = convert<std::vector<uint8_t>>();
            return converted.get<std::vector<uint8_t>>();
        } else {
            // swCError(kSwLogCategory_SwAny) << "Error: Not convertible to byte array. Current type: " << typeNameStr;
            return std::vector<uint8_t>();
        }
    }


    /**
     * @brief Convertit la valeur stockée dans SwAny en un SwString.
     *
     * Si le type interne n'est pas SwString mais peut être converti en SwString,
     * la conversion est tentée.
     *
     * @return SwString La valeur convertie si possible, sinon une instance vide avec message d'erreur.
     */
    SwString toString() const {
        if (typeNameStr == typeid(SwString).name()) {
            return get<SwString>();
        } else if (canConvert<SwString>()) {
            SwAny converted = convert<SwString>();
            return converted.get<SwString>();
        } else {
            // swCError(kSwLogCategory_SwAny) << "Error: Not convertible to SwString. Current type: " << typeNameStr;
            return SwString();
        }
    }

    /**
     * @brief Convertit la valeur stockée dans SwAny en un SwJsonValue.
     *
     * Si le type interne n'est pas SwJsonValue mais peut être converti en SwJsonValue,
     * la conversion est tentée.
     *
     * @return SwJsonValue La valeur convertie si possible, sinon une instance vide avec message d'erreur.
     */
    SwJsonValue toJsonValue() const {
        if (typeNameStr == typeid(SwJsonValue).name()) {
            return get<SwJsonValue>();
        } else if (canConvert<SwJsonValue>()) {
            SwAny converted = convert<SwJsonValue>();
            return converted.get<SwJsonValue>();
        } else {
            // swCError(kSwLogCategory_SwAny) << "Error: Not convertible to SwJsonValue. Current type: " << typeNameStr;
            return SwJsonValue();
        }
    }

    /**
     * @brief Convertit la valeur stockée dans SwAny en un SwJsonObject.
     *
     * Si le type interne n'est pas SwJsonObject mais peut être converti en SwJsonObject,
     * la conversion est tentée.
     *
     * @return SwJsonObject La valeur convertie si possible, sinon une instance vide avec message d'erreur.
     */
    SwJsonObject toJsonObject() const {
        if (typeNameStr == typeid(SwJsonObject).name()) {
            return get<SwJsonObject>();
        } else if (canConvert<SwJsonObject>()) {
            SwAny converted = convert<SwJsonObject>();
            return converted.get<SwJsonObject>();
        } else {
            // swCError(kSwLogCategory_SwAny) << "Error: Not convertible to SwJsonObject. Current type: " << typeNameStr;
            return SwJsonObject();
        }
    }

    /**
     * @brief Convertit la valeur stockée dans SwAny en un SwJsonArray.
     *
     * Si le type interne n'est pas SwJsonArray mais peut être converti en SwJsonArray,
     * la conversion est tentée.
     *
     * @return SwJsonArray La valeur convertie si possible, sinon une instance vide avec message d'erreur.
     */
    SwJsonArray toJsonArray() const {
        if (typeNameStr == typeid(SwJsonArray).name()) {
            return get<SwJsonArray>();
        } else if (canConvert<SwJsonArray>()) {
            SwAny converted = convert<SwJsonArray>();
            return converted.get<SwJsonArray>();
        } else {
            // swCError(kSwLogCategory_SwAny) << "Error: Not convertible to SwJsonArray. Current type: " << typeNameStr;
            return SwJsonArray();
        }
    }

    /**
     * @brief Performs the `metaType` operation.
     * @param typeNameStr Value passed to the method.
     * @return The requested meta Type.
     */
    SwMetaType::Type metaType() const { return SwMetaType::fromName(typeNameStr); }
    /**
     * @brief Performs the `typeId` operation.
     * @return The requested type Id.
     */
    int typeId() const { return static_cast<int>(metaType()); }


};




// -----------------------------------------------------------------------------
// Comparaison SwAny == SwAny
// -----------------------------------------------------------------------------
inline bool operator==(const SwAny& lhs, const SwAny& rhs)
{
    const std::string& lt = lhs.typeName();
    const std::string& rt = rhs.typeName();

    // Deux "vides"
    if (lt.empty() && rt.empty())
        return true;

    // Un seul vide
    if (lt.empty() || rt.empty())
        return false;

    // Même type → on compare directement la valeur sous-jacente
    if (lt == rt) {
        if (lt == typeid(bool).name())
            return lhs.get<bool>() == rhs.get<bool>();

        if (lt == typeid(int).name())
            return lhs.get<int>() == rhs.get<int>();

        if (lt == typeid(long long).name())
            return lhs.get<long long>() == rhs.get<long long>();

        if (!std::is_same<std::int64_t, long long>::value &&
            lt == typeid(std::int64_t).name())
            return lhs.get<std::int64_t>() == rhs.get<std::int64_t>();

        if (lt == typeid(unsigned int).name())
            return lhs.get<unsigned int>() == rhs.get<unsigned int>();

        if (lt == typeid(uint32_t).name())
            return lhs.get<uint32_t>() == rhs.get<uint32_t>();

        if (lt == typeid(unsigned long long).name())
            return lhs.get<unsigned long long>() == rhs.get<unsigned long long>();

        if (!std::is_same<std::uint64_t, unsigned long long>::value &&
            lt == typeid(std::uint64_t).name())
            return lhs.get<std::uint64_t>() == rhs.get<std::uint64_t>();

        if (lt == typeid(float).name())
            return lhs.get<float>() == rhs.get<float>();

        if (lt == typeid(double).name())
            return lhs.get<double>() == rhs.get<double>();

        if (lt == typeid(SwString).name())
            return lhs.get<SwString>() == rhs.get<SwString>();

        if (lt == typeid(std::string).name())
            return lhs.get<std::string>() == rhs.get<std::string>();

        if (lt == typeid(std::vector<uint8_t>).name())
            return lhs.get<std::vector<uint8_t>>() == rhs.get<std::vector<uint8_t>>();

        // Types dynamiques (SwJsonValue, SwJsonObject, SwJsonArray, SwJsonDocument…)
        // si leurs operator== sont définis, on peut les comparer pareil :
        if (lt == typeid(SwJsonValue).name())
            return lhs.get<SwJsonValue>() == rhs.get<SwJsonValue>();

        if (lt == typeid(SwJsonObject).name())
            return lhs.get<SwJsonObject>() == rhs.get<SwJsonObject>();

        if (lt == typeid(SwJsonArray).name())
            return lhs.get<SwJsonArray>() == rhs.get<SwJsonArray>();

        if (lt == typeid(SwJsonDocument).name())
            return lhs.get<SwJsonDocument>() == rhs.get<SwJsonDocument>();

        // Sinon, pour ce type précis, on tombe plus bas sur la comparaison sérialisée.
    }

    // Types différents : si les deux sont sérialisables, on compare leurs représentations string
    if (SwAny::isSerializable(lt) && SwAny::isSerializable(rt)) {
        return lhs.toString() == rhs.toString();
    }

    // Pas de règle de comparaison raisonnable → considérés différents
    return false;
}

using SwAnyList = SwList<SwAny>;

inline bool operator!=(const SwAny& lhs, const SwAny& rhs)
{
    return !(lhs == rhs);
}
