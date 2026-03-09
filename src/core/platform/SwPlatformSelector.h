#pragma once

/**
 * @file src/core/platform/SwPlatformSelector.h
 * @ingroup core_platform
 * @brief Declares the public interface exposed by SwPlatformSelector in the CoreSw core platform
 * abstraction layer.
 *
 * This header belongs to the CoreSw core platform abstraction layer. It encapsulates low-level
 * filesystem and standard-location services that differ across supported operating systems.
 *
 * Within that layer, this file focuses on the platform selector interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * The declarations in this area keep higher layers independent from direct POSIX or Win32 usage.
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

#include "SwPlatform.h"
#if defined(_WIN32)
#include "SwPlatformWin.h"
#else
#include "SwPlatformPosix.h"
#endif
