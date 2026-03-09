#pragma once

#include "SwList.h"
#include "SwString.h"

class SwBuildProject {
public:
    SwBuildProject();
    SwBuildProject(const SwString& rootDirAbs,
                   const SwString& sourceDirAbs,
                   const SwString& buildRootDirAbs,
                   const SwString& logRootDirAbs);

    SwString rootDirAbs() const { return rootDirAbs_; }
    SwString sourceDirAbs() const { return sourceDirAbs_; }
    SwString relativeSourceDir() const { return relativeSourceDir_; }

    SwString buildDirAbs() const { return buildDirAbs_; }
    SwString logDirAbs() const { return logDirAbs_; }

    SwList<SwString> dependenciesAbs() const { return dependenciesAbs_; }
    void setDependenciesAbs(const SwList<SwString>& depsAbs);

private:
    SwString rootDirAbs_;
    SwString sourceDirAbs_;
    SwString relativeSourceDir_;
    SwString buildDirAbs_;
    SwString logDirAbs_;
    SwList<SwString> dependenciesAbs_;
};
