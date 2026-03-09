#pragma once

#include "SwBuildProject.h"
#include "SwList.h"
#include "SwString.h"

class SwBuildDependencyResolver {
public:
    bool sort(SwList<SwBuildProject>& projects, SwString& errOut) const;
};
