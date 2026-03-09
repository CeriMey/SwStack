#pragma once

/**
 * @file
 * @ingroup core_swizio_nodes
 * @brief Declares hashing helpers for node-editor connection identifiers.
 *
 * The utilities in this header make `ConnectionId` usable in hash-based containers by
 * providing both a project-local hash functor and a `std::hash` specialization. That
 * keeps lookup code efficient without spreading ad-hoc hashing logic through the editor.
 */




#include "Definitions.hpp"

#include <cstddef>

namespace SwizioNodes {

struct ConnectionIdHash
{
    /**
     * @brief Performs the `operator` operation.
     * @param c Value passed to the method.
     * @return The requested operator.
     */
    std::size_t operator()(ConnectionId const& c) const noexcept
    {
        // Basic hash combine for the four integers.
        std::size_t h = 0;
        auto mix = [&h](std::size_t v) {
            h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        mix(static_cast<std::size_t>(c.outNodeId));
        mix(static_cast<std::size_t>(c.outPortIndex));
        mix(static_cast<std::size_t>(c.inNodeId));
        mix(static_cast<std::size_t>(c.inPortIndex));
        return h;
    }
};

} // namespace SwizioNodes

namespace std {
template <>
struct hash<SwizioNodes::ConnectionId> {
    /**
     * @brief Performs the `operator` operation.
     * @param c Value passed to the method.
     * @return The requested operator.
     */
    std::size_t operator()(SwizioNodes::ConnectionId const& c) const noexcept
    {
        return SwizioNodes::ConnectionIdHash{}(c);
    }
};
} // namespace std
