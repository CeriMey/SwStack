#pragma once

#include "SwApiCommand.h"

class SwApiNodesCommand : public SwApiCommand {
public:
    SwApiNodesCommand(const SwApiCli& cli,
                      SwApiIpcInspector& inspector,
                      const SwStringList& args,
                      SwObject* parent = nullptr);
    ~SwApiNodesCommand() override;

    void start() override;

private:
    void printUsage_() const;
    int cmdList_();
    int cmdInfo_();
    int cmdSaveAsFactory_();
    int cmdResetFactory_();
};
