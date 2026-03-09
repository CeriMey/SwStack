#pragma once

#include "SwApiCommand.h"

class SwApiAppsCommand : public SwApiCommand {
public:
    SwApiAppsCommand(const SwApiCli& cli,
                     SwApiIpcInspector& inspector,
                     const SwStringList& args,
                     SwObject* parent = nullptr);
    ~SwApiAppsCommand() override;

    void start() override;

private:
    void printUsage_() const;
};
