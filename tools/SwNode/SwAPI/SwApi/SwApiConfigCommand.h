#pragma once

#include "SwApiCommand.h"
#include "SwApiSubscriptionHandle.h"

class SwApiConfigCommand : public SwApiCommand {
public:
    SwApiConfigCommand(const SwApiCli& cli,
                       SwApiIpcInspector& inspector,
                       const SwStringList& args,
                       SwObject* parent = nullptr);
    ~SwApiConfigCommand() override;

    void start() override;

private:
    void printUsage_() const;
    int cmdDump_();
    int cmdGet_();
    int cmdSet_();
    int cmdSendAll_();
    void cmdWatch_();

    SwApiSubscriptionHandle subscription_;
};
