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

#include "SwRemoteObjectComponentRegistry.h"
#include "SwString.h"

namespace sw {
namespace component {
namespace plugin {

inline const SwString& registerSymbolV1() {
    static const SwString k("swRegisterRemoteObjectComponentsV1");
    return k;
}

typedef bool (*RegisterFnV1)(SwRemoteObjectComponentRegistry* registry);

namespace detail {

struct ComponentNode {
    SwString typeName;
    SwRemoteObjectComponentRegistry::CreateFn create;
    SwRemoteObjectComponentRegistry::DestroyFn destroy;
    ComponentNode* next;
};

inline ComponentNode*& head() {
    static ComponentNode* h = nullptr;
    return h;
}

struct AutoRegister {
    explicit AutoRegister(ComponentNode* n) {
        if (!n) return;
        n->next = head();
        head() = n;
    }
};

inline SwString normalizeComponentTypeName(SwString raw) {
    raw = raw.trimmed();
    raw.replace("\\", "/");
    raw.replace("::", "/");
    raw.replace(" ", "");
    raw.replace("\t", "");
    while (raw.startsWith("/")) raw = raw.mid(1);
    while (raw.contains("//")) raw.replace("//", "/");
    while (raw.endsWith("/")) raw = raw.left(static_cast<int>(raw.size()) - 1);
    return raw;
}

#ifdef _WIN32
#  define SW_COMPONENT_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#  define SW_COMPONENT_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

} // namespace detail

} // namespace plugin
} // namespace component
} // namespace sw

// Register a SwRemoteObject-derived component in a plugin.
// This macro must appear in a translation unit compiled into a shared library,
// and the library must be built with the compile definition `SW_COMPONENT_PLUGIN=1`.
//
// The `type` string registered in the container registry is derived from the C++ type name:
// - spaces are stripped
// - `::` is converted to `/` (ex: `demo::PingComponent` -> `demo/PingComponent`)
//
// Example (in a plugin .cpp):
//   namespace demo { class MyComp : public SwRemoteObject { ... }; }
//   SW_REGISTER_COMPONENT_NODE(demo::MyComp);

#define SW_DETAIL_CONCAT_INNER_(a, b) a##b
#define SW_DETAIL_CONCAT_(a, b) SW_DETAIL_CONCAT_INNER_(a, b)

#ifdef __COUNTER__
#define SW_DETAIL_UNIQUE_ID_ __COUNTER__
#else
#define SW_DETAIL_UNIQUE_ID_ __LINE__
#endif

#define SW_REGISTER_COMPONENT_NODE(ClassType) SW_REGISTER_COMPONENT_NODE_IMPL_(ClassType, SW_DETAIL_UNIQUE_ID_)

#define SW_REGISTER_COMPONENT_NODE_AS(ClassType, TypeNameLiteral) \
    SW_REGISTER_COMPONENT_NODE_AS_IMPL_(ClassType, TypeNameLiteral, SW_DETAIL_UNIQUE_ID_)

#define SW_REGISTER_COMPONENT_NODE_IMPL_(ClassType, UniqueId)                                       \
    namespace {                                                                                      \
    static SwRemoteObject* SW_DETAIL_CONCAT_(swipc_create_, UniqueId)(const SwString& sysName,       \
                                                                     const SwString& nameSpace,      \
                                                                     const SwString& objectName,     \
                                                                     SwObject* parent) {             \
        return new ClassType(sysName, nameSpace, objectName, parent);                                \
    }                                                                                                \
    static void SW_DETAIL_CONCAT_(swipc_destroy_, UniqueId)(SwRemoteObject* instance) {              \
        delete static_cast<ClassType*>(instance);                                                    \
    }                                                                                                \
    static ::sw::component::plugin::detail::ComponentNode SW_DETAIL_CONCAT_(swipc_node_, UniqueId){  \
        ::sw::component::plugin::detail::normalizeComponentTypeName(SwString(#ClassType)),           \
        &SW_DETAIL_CONCAT_(swipc_create_, UniqueId),                                                 \
        &SW_DETAIL_CONCAT_(swipc_destroy_, UniqueId),                                                \
        nullptr};                                                                                    \
    static ::sw::component::plugin::detail::AutoRegister SW_DETAIL_CONCAT_(swipc_autoreg_, UniqueId)(\
        &SW_DETAIL_CONCAT_(swipc_node_, UniqueId));                                                  \
    }

#define SW_REGISTER_COMPONENT_NODE_AS_IMPL_(ClassType, TypeNameLiteral, UniqueId)                    \
    namespace {                                                                                      \
    static SwRemoteObject* SW_DETAIL_CONCAT_(swipc_create_, UniqueId)(const SwString& sysName,       \
                                                                     const SwString& nameSpace,      \
                                                                     const SwString& objectName,     \
                                                                     SwObject* parent) {             \
        return new ClassType(sysName, nameSpace, objectName, parent);                                \
    }                                                                                                \
    static void SW_DETAIL_CONCAT_(swipc_destroy_, UniqueId)(SwRemoteObject* instance) {              \
        delete static_cast<ClassType*>(instance);                                                    \
    }                                                                                                \
    static ::sw::component::plugin::detail::ComponentNode SW_DETAIL_CONCAT_(swipc_node_, UniqueId){  \
        ::sw::component::plugin::detail::normalizeComponentTypeName(SwString(TypeNameLiteral)),      \
        &SW_DETAIL_CONCAT_(swipc_create_, UniqueId),                                                 \
        &SW_DETAIL_CONCAT_(swipc_destroy_, UniqueId),                                                \
        nullptr};                                                                                    \
    static ::sw::component::plugin::detail::AutoRegister SW_DETAIL_CONCAT_(swipc_autoreg_, UniqueId)(\
        &SW_DETAIL_CONCAT_(swipc_node_, UniqueId));                                                  \
    }

// Default exported entry point (v1) for plugins.
// Compiled only when SW_COMPONENT_PLUGIN is defined (set by the plugin CMake target).
#ifdef SW_COMPONENT_PLUGIN
SW_COMPONENT_PLUGIN_EXPORT bool swRegisterRemoteObjectComponentsV1(SwRemoteObjectComponentRegistry* registry) {
    if (!registry) return false;
    ::sw::component::plugin::detail::ComponentNode* n = ::sw::component::plugin::detail::head();
    while (n) {
        if (!n->typeName.isEmpty() && n->create) {
            (void)registry->registerComponent(n->typeName, n->create, n->destroy);
        }
        n = n->next;
    }
    return true;
}
#endif
