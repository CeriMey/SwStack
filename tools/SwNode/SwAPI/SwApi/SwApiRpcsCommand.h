#pragma once

#include "SwApiCommand.h"

class SwApiRpcsCommand : public SwApiCommand {
public:
    SwApiRpcsCommand(const SwApiCli& cli,
                     SwApiIpcInspector& inspector,
                     const SwStringList& args,
                     SwObject* parent = nullptr);
    ~SwApiRpcsCommand() override;

    void start() override;

private:
    void printUsage_() const;
    int cmdList_();
    int cmdCall_();
};
