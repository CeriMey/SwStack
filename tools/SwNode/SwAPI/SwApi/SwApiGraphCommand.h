#pragma once

#include "SwApiCommand.h"

class SwApiGraphCommand : public SwApiCommand {
public:
    SwApiGraphCommand(const SwApiCli& cli,
                      SwApiIpcInspector& inspector,
                      const SwStringList& args,
                      SwObject* parent = nullptr);
    ~SwApiGraphCommand() override;

    void start() override;

private:
    void printUsage_() const;
    int cmdConnections_();
};
