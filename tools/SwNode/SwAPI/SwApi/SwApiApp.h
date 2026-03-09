#pragma once

#include "SwObject.h"

#include "SwApiCli.h"
#include "SwApiIpcInspector.h"

class SwCoreApplication;
class SwApiCommand;

class SwApiApp : public SwObject {
public:
    SwApiApp(SwCoreApplication& app, int argc, char** argv, SwObject* parent = nullptr);
    ~SwApiApp() override;

private:
    void run_();
    void printUsage_() const;
    SwApiCommand* makeCommand_();

    SwCoreApplication& app_;
    SwApiCli cli_;
    SwApiIpcInspector inspector_;
    SwApiCommand* cmd_{nullptr};
};
