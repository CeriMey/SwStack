#pragma once

#include "SwCoreApplication.h"
#include "SwString.h"

class SwBuildOptions {
public:
    SwBuildOptions();

    static bool fromApplication(const SwCoreApplication& app, SwBuildOptions& out, SwString& errOut);

    bool showHelp() const { return showHelp_; }
    bool clean() const { return clean_; }
    bool configureOnly() const { return configureOnly_; }
    bool buildOnly() const { return buildOnly_; }
    bool installEnabled() const { return installEnabled_; }
    bool includeNested() const { return includeNested_; }
    bool dryRun() const { return dryRun_; }
    bool verbose() const { return verbose_; }

    SwString rootDirAbs() const { return rootDirAbs_; }
    SwString scanDirAbs() const { return scanDirAbs_; }
    SwString buildType() const { return buildType_; }
    SwString cmakeBin() const { return cmakeBin_; }
    SwString generator() const { return generator_; }

    SwString buildRootDir() const { return buildRootDir_; }
    SwString logRootDir() const { return logRootDir_; }
    SwString installRootDir() const { return installRootDir_; }

private:
    bool showHelp_{false};
    bool clean_{false};
    bool configureOnly_{false};
    bool buildOnly_{false};
    bool installEnabled_{true};
    bool includeNested_{true};
    bool dryRun_{false};
    bool verbose_{false};

    SwString rootDirAbs_;
    SwString scanDirAbs_;
    SwString buildType_;
    SwString cmakeBin_;
    SwString generator_;

    SwString buildRootDir_;
    SwString logRootDir_;
    SwString installRootDir_;
};
