#include "SwBuildProject.h"

#include "SwBuildUtils.h"

SwBuildProject::SwBuildProject() = default;

SwBuildProject::SwBuildProject(const SwString& rootDirAbs,
                               const SwString& sourceDirAbs,
                               const SwString& buildRootDirAbs,
                               const SwString& logRootDirAbs)
    : rootDirAbs_(swbuild::normalizePath(rootDirAbs)),
      sourceDirAbs_(swbuild::normalizePath(sourceDirAbs)) {
    relativeSourceDir_ = swbuild::relativeToRoot(rootDirAbs_, sourceDirAbs_);

    SwString buildRootNorm = swbuild::normalizePath(buildRootDirAbs);
    while (buildRootNorm.endsWith("/")) buildRootNorm.chop(1);
    SwString logRootNorm = swbuild::normalizePath(logRootDirAbs);
    while (logRootNorm.endsWith("/")) logRootNorm.chop(1);

    buildDirAbs_ = swbuild::joinPath(buildRootNorm, relativeSourceDir_);
    logDirAbs_ = swbuild::joinPath(logRootNorm, relativeSourceDir_);
}

void SwBuildProject::setDependenciesAbs(const SwList<SwString>& depsAbs) {
    dependenciesAbs_ = depsAbs;
}
