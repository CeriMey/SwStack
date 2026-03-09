#include "SwBuildDependencyResolver.h"

#include "SwMap.h"

#include <functional>

namespace {
enum VisitMark {
    Unvisited = 0,
    Visiting = 1,
    Done = 2
};
} // namespace

bool SwBuildDependencyResolver::sort(SwList<SwBuildProject>& projects, SwString& errOut) const {
    errOut.clear();
    if (projects.size() <= 1) return true;

    SwMap<SwString, SwBuildProject*> projectByDir;
    for (int i = 0; i < projects.size(); ++i) {
        projectByDir.insert(projects[i].sourceDirAbs(), &projects[i]);
    }

    SwMap<SwString, int> marks;
    SwList<SwBuildProject> ordered;
    ordered.reserve(projects.size());

    std::function<bool(const SwString&)> visit = [&](const SwString& dir) -> bool {
        const int mark = marks.value(dir, Unvisited);
        if (mark == Done) return true;
        if (mark == Visiting) {
            errOut = SwString("dependency cycle detected at: ") + dir;
            return false;
        }

        if (!projectByDir.contains(dir)) {
            errOut = SwString("unknown dependency project: ") + dir;
            return false;
        }

        marks[dir] = Visiting;
        SwBuildProject* p = projectByDir[dir];
        const SwList<SwString> deps = p->dependenciesAbs();
        for (int i = 0; i < deps.size(); ++i) {
            const SwString dep = deps[i];
            if (!projectByDir.contains(dep)) {
                errOut = SwString("missing dependency: ") + dep + SwString(" (required by ") + dir + SwString(")");
                return false;
            }
            if (!visit(dep)) {
                return false;
            }
        }

        marks[dir] = Done;
        ordered.append(*p);
        return true;
    };

    for (int i = 0; i < projects.size(); ++i) {
        const SwString dir = projects[i].sourceDirAbs();
        if (!visit(dir)) return false;
    }

    projects = ordered;
    return true;
}
