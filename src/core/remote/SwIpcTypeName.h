#pragma once

#include "SwString.h"
#include "SwByteArray.h"
#include <cstdint>
#include <type_traits>
#include <typeinfo>

namespace sw { namespace ipc { namespace detail {
// Registry descriptions are a wire schema, not a compiler function signature.
// Keep numeric widths explicit (notably unsigned long on LP64 versus LLP64).
template<class T> inline SwString wireTypeName() {
    using V = typename std::decay<T>::type;
    if (std::is_same<V, bool>::value) return "bool";
    if (std::is_same<V, SwString>::value) return "SwString";
    if (std::is_same<V, SwByteArray>::value) return "SwByteArray";
    if (std::is_same<V, float>::value) return "float";
    if (std::is_same<V, double>::value) return "double";
    if (std::is_integral<V>::value) {
        const SwString prefix = std::is_signed<V>::value ? "int" : "uint";
        return prefix + SwString::number(sizeof(V) * 8) + "_t";
    }
    // Unknown user types remain deliberately opaque to dynamic tools.
    return SwString(typeid(V).name());
}
template<class... Args> inline SwString wireTupleName() {
    SwString out("sw::ipc::tuple<");
    bool first = true;
    using Expand = int[];
    (void)Expand{0, ((out += (first ? "" : ",") + wireTypeName<Args>(), first = false), 0)...};
    return out + ">";
}
}}}
