#include "SwCoreApplication.h"

#include "SwBuildController.h"
#include "SwBuildOptions.h"

#include <iostream>

static void printUsage_(const char* exe) {
    std::cout
        << "Usage:\n"
        << "  " << (exe ? exe : "SwBuild") << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  --root <dir>            Repo root (default: current directory)\n"
        << "  --scan <dir>            Directory to scan for CMakeLists.txt (default: src)\n"
        << "  --build_root <dir>      Build directory root (default: <root>/build)\n"
        << "  --build_type <type>     Release|Debug|RelWithDebInfo|MinSizeRel (default: Release)\n"
        << "  --cmake <path>          CMake executable (default: cmake)\n"
        << "  --generator <name>      CMake generator (optional)\n"
        << "  --clean                 Remove per-project build dirs before configure\n"
        << "  --configure_only        Only run cmake configure/generate\n"
        << "  --build_only            Only run cmake build (assumes already configured)\n"
        << "  --no_install            Skip install copy stage\n"
        << "  --include_nested        Include nested CMakeLists.txt projects (default)\n"
        << "  --exclude_nested        Only keep top-level projects\n"
        << "  --dry_run               Print actions without running them\n"
        << "  --verbose               Mirror process stdout/stderr to console\n"
        << "  --help, -h              Show this help\n";
}

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    SwBuildOptions options;
    SwString err;
    if (!SwBuildOptions::fromApplication(app, options, err)) {
        if (!err.isEmpty()) {
            std::cerr << err.toStdString() << "\n\n";
        }
        printUsage_(argc > 0 ? argv[0] : nullptr);
        return 2;
    }

    if (options.showHelp()) {
        printUsage_(argc > 0 ? argv[0] : nullptr);
        return 0;
    }

    SwBuildController controller(options);
    controller.start();

    return app.exec();
}
