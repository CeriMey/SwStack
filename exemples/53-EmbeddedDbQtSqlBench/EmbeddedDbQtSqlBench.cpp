#include <QCoreApplication>

#include "EmbeddedDbQtSqlBenchQt.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    swEmbeddedDbQtSqlBench::BenchOptions options;
    bool helpRequested = false;
    if (!swEmbeddedDbQtSqlBench::parseBenchOptions(argc, argv, options, helpRequested)) {
        return helpRequested ? 0 : 1;
    }

    swEmbeddedDbQtSqlBench::EmbeddedDbQtSqlBenchRunner runner(options, app);
    return runner.run();
}
