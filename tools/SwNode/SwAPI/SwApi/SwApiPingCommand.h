#pragma once

#include "SwApiCommand.h"

class SwApiPingCommand : public SwApiCommand {
public:
    SwApiPingCommand(const SwApiCli& cli,
                     SwApiIpcInspector& inspector,
                     const SwStringList& args,
                     SwObject* parent = nullptr);
    ~SwApiPingCommand() override;

    void start() override;

private:
    void printUsage_() const;
};

