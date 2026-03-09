#include "SwApiCommand.h"

SwApiCommand::SwApiCommand(const SwApiCli& cli,
                           SwApiIpcInspector& inspector,
                           const SwStringList& args,
                           SwObject* parent)
    : SwObject(parent), cli_(cli), inspector_(inspector), args_(args) {}

SwApiCommand::~SwApiCommand() = default;

void SwApiCommand::finish(int code) { finished(code); }
