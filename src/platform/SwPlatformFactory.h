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
 * @file src/platform/SwPlatformFactory.h
 * @ingroup platform_backends
 * @brief Declares the public interface exposed by SwPlatformFactory in the CoreSw platform
 * integration layer.
 *
 * This header belongs to the CoreSw platform integration layer. It exposes top-level platform
 * integration contracts shared by the GUI and rendering backends.
 *
 * Within that layer, this file focuses on the platform factory interface. The declarations
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
 * These declarations define how platform-specific backends plug into otherwise portable framework
 * code.
 *
 */


/**
 * @file
 * @brief Declares the helper used to create the default platform integration.
 *
 * This header is the narrow compile-time dispatch point between the generic GUI
 * runtime and the concrete platform backend available for the current target.
 * It keeps platform selection localized so the rest of the stack can depend on
 * the abstract SwPlatformIntegration interface.
 */

#include <memory>

#include "platform/SwPlatformIntegration.h"

#if defined(_WIN32)
#include "platform/win/SwWin32PlatformIntegration.h"
#elif defined(__linux__)
#include "platform/x11/SwX11PlatformIntegration.h"
#endif

/**
 * @brief Creates the platform integration that matches the current build target.
 *
 * The returned object owns the native windowing, event pumping, and painting
 * hooks required by SwGuiApplication and related GUI infrastructure.
 */
inline std::unique_ptr<SwPlatformIntegration> SwCreateDefaultPlatformIntegration() {
#if defined(_WIN32)
    return SwCreateWin32PlatformIntegration();
#elif defined(__linux__)
    return SwCreateX11PlatformIntegration();
#else
    return nullptr;
#endif
}
