#include "SwCoreApplication.h"
#include "SwDir.h"
#include "SwFile.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwTimer.h"

#ifndef SWLAUNCH_FIXTURE_VERSION
#define SWLAUNCH_FIXTURE_VERSION "unknown"
#endif

static bool isAbsPathFixture_(const SwString& path) {
    if (path.isEmpty()) return false;
    if (path.startsWith("/") || path.startsWith("\\")) return true;
    return path.size() >= 2 && path[1] == ':';
}

static SwString joinPathFixture_(const SwString& base, const SwString& leaf) {
    if (base.isEmpty()) return leaf;
    SwString out = base;
    out.replace("\\", "/");
    while (out.endsWith("/")) out.chop(1);
    SwString normalizedLeaf = leaf;
    normalizedLeaf.replace("\\", "/");
    while (normalizedLeaf.startsWith("/")) normalizedLeaf = normalizedLeaf.mid(1);
    return out + "/" + normalizedLeaf;
}

static SwString resolveFixturePath_(const SwString& path) {
    if (path.isEmpty()) return SwString();
    if (isAbsPathFixture_(path)) return swDirPlatform().absolutePath(path);
    return swDirPlatform().absolutePath(joinPathFixture_(SwDir::currentPath(), path));
}

static bool readConfigObject_(const SwString& configPath, SwJsonObject& outConfig, SwString& errOut) {
    outConfig = SwJsonObject();
    errOut.clear();

    if (configPath.isEmpty()) {
        errOut = "missing --config_file";
        return false;
    }
    if (!SwFile::isFile(configPath)) {
        errOut = SwString("config file not found: ") + configPath;
        return false;
    }

    SwFile file(configPath);
    if (!file.open(SwFile::Read)) {
        errOut = SwString("failed to open config file: ") + configPath;
        return false;
    }

    SwJsonDocument document;
    if (!document.loadFromJson(file.readAll(), errOut) || !document.isObject()) {
        if (errOut.isEmpty()) errOut = "config root must be an object";
        return false;
    }

    outConfig = document.object();
    return true;
}

static bool appendLine_(const SwString& path, const SwString& line, SwString& errOut) {
    errOut.clear();
    SwFile file(path);
    const SwString directory = file.getDirectory();
    if (!directory.isEmpty() && !SwDir::mkpathAbsolute(directory, false)) {
        errOut = SwString("failed to create directory: ") + directory;
        return false;
    }
    if (!file.open(SwFile::Append)) {
        errOut = SwString("failed to open append file: ") + path;
        return false;
    }
    if (!file.write(line)) {
        errOut = SwString("failed to append file: ") + path;
        file.close();
        return false;
    }
    file.close();
    return true;
}

static bool writeTextFixture_(const SwString& path, const SwString& text, SwString& errOut) {
    errOut.clear();
    SwFile file(path);
    const SwString directory = file.getDirectory();
    if (!directory.isEmpty() && !SwDir::mkpathAbsolute(directory, false)) {
        errOut = SwString("failed to create directory: ") + directory;
        return false;
    }
    if (!file.open(SwFile::Write)) {
        errOut = SwString("failed to open file for write: ") + path;
        return false;
    }
    if (!file.write(text)) {
        errOut = SwString("failed to write file: ") + path;
        file.close();
        return false;
    }
    file.close();
    return true;
}

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    SwJsonObject config;
    SwString err;
    if (!readConfigObject_(app.getArgument("config_file", SwString()), config, err)) {
        swError() << "[fixture-node]" << err;
        return 2;
    }

    const SwJsonObject params = config.contains("params") && config["params"].isObject()
                                    ? config["params"].toObject()
                                    : SwJsonObject();

    const SwString eventLogPath = resolveFixturePath_(params.contains("event_log")
                                                          ? SwString(params["event_log"].toString())
                                                          : SwString("event_log.txt"));
    const SwString payloadPath = resolveFixturePath_(params.contains("payload_file")
                                                         ? SwString(params["payload_file"].toString())
                                                         : SwString("payload.txt"));
    const SwString readyPath = resolveFixturePath_(params.contains("ready_file")
                                                       ? SwString(params["ready_file"].toString())
                                                       : SwString("ready.txt"));

    SwString payloadText;
    if (SwFile::isFile(payloadPath)) {
        SwFile payloadFile(payloadPath);
        if (payloadFile.open(SwFile::Read)) {
            payloadText = payloadFile.readAll().trimmed();
            payloadFile.close();
        }
    }

    const SwString marker = SwString("version=") + SWLAUNCH_FIXTURE_VERSION + ";payload=" + payloadText + "\n";
    if (!appendLine_(eventLogPath, marker, err)) {
        swError() << "[fixture-node]" << err;
        return 3;
    }
    if (!writeTextFixture_(readyPath, marker, err)) {
        swError() << "[fixture-node]" << err;
        return 4;
    }

    const int durationMs = app.getArgument("duration_ms", "0").toInt();
    if (durationMs > 0) {
        SwTimer::singleShot(durationMs, [&app]() { app.quit(); });
    }

    return app.exec();
}
