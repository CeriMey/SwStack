#pragma once

#include "SwRemoteObject.h"
#include "SwString.h"

class SwLaunchTraceConfig : public SwRemoteObject {
public:
    SwLaunchTraceConfig(const SwString& sysName,
                        const SwString& configRootDir,
                        const SwString& baseDir,
                        SwObject* parent = nullptr);

private:
    void apply_();

    SwString baseDir_{};
    bool toConsole_{true};
    bool toFile_{false};
    SwString filePath_{};
    SwString filterRegex_{};
};

