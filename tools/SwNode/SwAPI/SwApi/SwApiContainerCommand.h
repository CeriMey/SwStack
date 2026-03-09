#pragma once

#include "SwApiCommand.h"

class SwApiContainerCommand : public SwApiCommand {
public:
    SwApiContainerCommand(const SwApiCli& cli,
                          SwApiIpcInspector& inspector,
                          const SwStringList& args,
                          SwObject* parent = nullptr);
    ~SwApiContainerCommand() override;

    void start() override;

private:
    void printUsage_() const;
    int cmdStatus_();
    int cmdPlugins_();
    int cmdPlugin_();
    int cmdComponents_();
    int cmdComponent_();
};
