#pragma once

#include "SwString.h"

#include "SwBuildOptions.h"
#include "SwBuildProject.h"

class SwBuildInstaller {
public:
    bool install(const SwBuildProject& project, const SwBuildOptions& options, SwString& errOut) const;
};
