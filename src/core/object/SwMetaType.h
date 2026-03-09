#pragma once

/**
 * @file src/core/object/SwMetaType.h
 * @ingroup core_object
 * @brief Declares the public interface exposed by SwMetaType in the CoreSw object model layer.
 *
 * This header belongs to the CoreSw object model layer. It defines parent and child ownership,
 * runtime typing, and the signal-slot machinery that many other modules build upon.
 *
 * Within that layer, this file focuses on the meta type interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwMetaType.
 *
 * Type-oriented declarations here establish shared vocabulary for the surrounding subsystem so
 * multiple components can exchange data and configuration without ad-hoc conventions.
 *
 * Object-model declarations here establish how instances are identified, connected, owned, and
 * moved across execution contexts.
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

#include <string>
#include <typeinfo>
#include <vector>
#include <cstdint>

#include "SwString.h"
#include "SwJsonValue.h"
#include "SwJsonObject.h"
#include "SwJsonArray.h"

#ifdef Bool
#undef Bool
#endif

class SwMetaType {
public:
    enum Type {
        UnknownType = 0,
        Bool,
        Int,
        UInt,
        Double,
        Float,
        String,        ///< SwString
        StdString,     ///< std::string
        ByteArray,     ///< std::vector<uint8_t>
        JsonValue,
        JsonObject,
        JsonArray,
        Any
    };

    /**
     * @brief Performs the `fromName` operation.
     * @param name Value passed to the method.
     * @return The requested from Name.
     */
    static Type fromName(const std::string& name) {
        if (name.empty()) return UnknownType;

#define SW_MT_CHECK(T, enumValue) \
        if (name == typeid(T).name()) return enumValue;

        SW_MT_CHECK(bool, Bool);
        SW_MT_CHECK(int, Int);
        SW_MT_CHECK(unsigned int, UInt);
        SW_MT_CHECK(uint32_t, UInt);
        SW_MT_CHECK(double, Double);
        SW_MT_CHECK(float, Float);
        SW_MT_CHECK(class SwString, String);
        SW_MT_CHECK(std::string, StdString);
        SW_MT_CHECK(std::vector<uint8_t>, ByteArray);
        SW_MT_CHECK(SwJsonValue, JsonValue);
        SW_MT_CHECK(SwJsonObject, JsonObject);
        SW_MT_CHECK(SwJsonArray, JsonArray);

#undef SW_MT_CHECK
        return UnknownType;
    }

    template<typename T>
    /**
     * @brief Returns the current id.
     * @return The current id.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static Type id() {
        return fromName(typeid(T).name());
    }
};
