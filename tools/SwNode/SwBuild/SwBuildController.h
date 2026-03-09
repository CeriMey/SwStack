#pragma once

#include "SwObject.h"
#include "SwString.h"

#include "SwBuildOptions.h"
#include "SwBuildProject.h"

class SwFile;
class SwProcess;

class SwBuildController : public SwObject {
public:
    explicit SwBuildController(const SwBuildOptions& options, SwObject* parent = nullptr);

    void start();

private:
    enum class Stage {
        Configure,
        Build
    };

    bool prepareWorkspace_(SwString& errOut);
    bool discoverProjects_(SwString& errOut);

    void startCurrentStage_();
    bool startConfigure_(const SwBuildProject& project, SwString& errOut);
    bool startBuild_(const SwBuildProject& project, SwString& errOut);

    bool startProcess_(const SwString& label,
                       const SwString& program,
                       const SwStringList& args,
                       const SwString& workingDir,
                       const SwString& logFilePath,
                       SwString& errOut);

    void handleProcessStdOut_();
    void handleProcessStdErr_();
    void handleProcessTerminated_(int exitCode);

    bool finalizeBuildProject_(const SwBuildProject& project, SwString& errOut);
    void failAndExit_(int exitCode, const SwString& message);

private:
    SwBuildOptions options_;
    SwList<SwBuildProject> projects_;
    int currentIndex_{0};
    Stage stage_{Stage::Configure};

    SwProcess* process_{nullptr};
    SwFile* logFile_{nullptr};
    SwString currentLabel_;
    SwString currentLogPath_;
};
