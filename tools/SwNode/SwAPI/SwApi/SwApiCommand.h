#pragma once

#include "SwObject.h"

#include "SwApiCli.h"
#include "SwApiIpcInspector.h"

class SwApiCommand : public SwObject {
public:
    SwApiCommand(const SwApiCli& cli,
                 SwApiIpcInspector& inspector,
                 const SwStringList& args,
                 SwObject* parent = nullptr);
    ~SwApiCommand() override;

    DECLARE_SIGNAL(finished, int)

    virtual void start() = 0;

protected:
    void finish(int code);

    const SwApiCli& cli() const { return cli_; }
    SwApiIpcInspector& inspector() { return inspector_; }
    const SwStringList& args() const { return args_; }

private:
    const SwApiCli& cli_;
    SwApiIpcInspector& inspector_;
    SwStringList args_;
};
