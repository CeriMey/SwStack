#pragma once

#include "SwString.h"

struct SwCreatorPaletteEntry {
    SwString category;
    SwString className;
    SwString displayName;
    bool isLayout{false};
};

