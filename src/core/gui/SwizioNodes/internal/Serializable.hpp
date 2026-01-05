#pragma once

#include "core/types/SwJsonObject.h"

namespace SwizioNodes {

class Serializable
{
public:
    virtual ~Serializable() = default;

    virtual SwJsonObject save() const { return {}; }

    virtual void load(SwJsonObject const& /*p*/) {}
};

} // namespace SwizioNodes

