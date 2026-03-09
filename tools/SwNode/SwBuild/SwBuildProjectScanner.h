#pragma once

#include "SwBuildOptions.h"
#include "SwBuildProject.h"
#include "SwList.h"
#include "SwString.h"

class SwBuildProjectScanner {
public:
    SwList<SwBuildProject> scan(const SwBuildOptions& options, SwString& errOut) const;
};
