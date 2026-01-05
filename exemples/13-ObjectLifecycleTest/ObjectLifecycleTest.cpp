#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "SwCoreApplication.h"
#include "SwObject.h"
#include "SwTimer.h"

struct TestResult {
    std::string name;
    bool success;
    std::string detail;
};

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    std::vector<TestResult> results;
    auto addResult = [&](const std::string& name, bool success, const std::string& detail = std::string()) {
        results.push_back({name, success, detail});
        std::cout << "[TEST] " << name << " -> " << (success ? "PASS" : "FAIL");
        if (!detail.empty()) {
            std::cout << " (" << detail << ")";
        }
        std::cout << std::endl;
    };

    std::vector<std::string> logs;
    auto logEvent = [&](const std::string& entry) {
        logs.push_back(entry);
        std::cout << "[LOG] " << entry << std::endl;
    };

    auto hasLog = [&](const std::string& entry) {
        return std::find(logs.begin(), logs.end(), entry) != logs.end();
    };

    auto watchObject = [&](SwObject* object, const std::string& label) {
        SwObject::connect(object, &SwObject::destroyed, [&, label]() {
            logEvent(label + ":destroyed");
        }, DirectConnection);
        SwObject::connect(object, &SwObject::childRemoved, [&, label](SwObject* child) {
            logEvent(label + ":childRemoved");
        }, DirectConnection);
    };

    SwObject* root = new SwObject();
    watchObject(root, "root");

    SwObject* childA = new SwObject(root);
    watchObject(childA, "childA");

    SwObject* childB = new SwObject(root);
    watchObject(childB, "childB");

    SwObject* grandChild = new SwObject(childA);
    watchObject(grandChild, "grandChild");

    bool rootNotifiedChildA = false;
    SwObject::connect(root, &SwObject::childRemoved, [&](SwObject* child) {
        if (child == childA) {
            rootNotifiedChildA = true;
        }
    }, DirectConnection);

    delete childA;
    childA = nullptr;

    bool grandChildDestroyed = hasLog("grandChild:destroyed");
    addResult("Deleting parent destroys grandchildren", grandChildDestroyed);
    addResult("Parent notified when child removed", rootNotifiedChildA);

    SwObject* delayed = new SwObject(root);
    watchObject(delayed, "delayed");

    bool delayedDestroyed = false;
    bool deleteLaterTimeout = false;
    SwObject::connect(delayed, &SwObject::destroyed, [&]() {
        delayedDestroyed = true;
        app.quit();
    }, DirectConnection);

    delayed->deleteLater();

    SwTimer::singleShot(200, [&]() {
        deleteLaterTimeout = true;
        app.quit();
    });

    app.exec();

    addResult("deleteLater emits destruction signals",
              delayedDestroyed && !deleteLaterTimeout,
              deleteLaterTimeout ? "timeout waiting for deleteLater" : "");

    delete root;
    root = nullptr;

    addResult("Deleting root destroys remaining child", hasLog("childB:destroyed"));
    addResult("Root destruction emits signals", hasLog("root:destroyed"));

    std::cout << "\n===== Object Lifecycle Test Summary =====\n";
    for (const auto& result : results) {
        std::cout << (result.success ? "[PASS] " : "[FAIL] ") << result.name;
        if (!result.detail.empty()) {
            std::cout << " -> " << result.detail;
        }
        std::cout << std::endl;
    }

    return 0;
}
