#pragma once
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
    static Type id() {
        return fromName(typeid(T).name());
    }
};
