#pragma once

/**
 * @file
 * @ingroup core_fs
 * @brief Declares `SwStandardLocation`, the static entry point for well-known paths.
 *
 * This facade routes location requests to the platform-specific provider selected
 * by the stack. Callers use it to resolve user, cache, temp, and application paths
 * without embedding operating-system conditionals in higher-level code.
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



#include "SwStandardLocationDefs.h"
#include "platform/SwPlatformSelector.h"

class SwStandardLocation {
public:
    using PathType = SwStandardPathType;
    using Location = SwStandardLocationId;

    /**
     * @brief Performs the `standardLocation` operation.
     * @param type Value passed to the method.
     * @return The requested standard Location.
     */
    static SwString standardLocation(Location type) {
        return swStandardLocationProvider().standardLocation(type);
    }

    /**
     * @brief Performs the `convertPath` operation.
     * @param path Path used by the operation.
     * @param type Value passed to the method.
     * @return The requested convert Path.
     */
    static SwString convertPath(const SwString& path, PathType type) {
        return swStandardLocationProvider().convertPath(path, type);
    }
};
