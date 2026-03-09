#pragma once

#include "SwApiCommand.h"
#include "SwApiSubscriptionHandle.h"

class SwApiSignalsCommand : public SwApiCommand {
public:
    SwApiSignalsCommand(const SwApiCli& cli,
                        SwApiIpcInspector& inspector,
                        const SwStringList& args,
                        SwObject* parent = nullptr);
    ~SwApiSignalsCommand() override;

    void start() override;

private:
    void printUsage_() const;
    int cmdList_();
    void cmdEcho_();
    int cmdPublish_();

    SwApiSubscriptionHandle subscription_;
};
