#include "SwCoreApplication.h"
#include "SwDir.h"
#include "SwFile.h"
#include "SwFileInfo.h"
#include "SwInstaller.h"
#include "SwStandardPaths.h"

#include <filesystem>
#include <iostream>
#include <vector>

namespace {

static SwString sha256Hex(const SwByteArray& bytes) {
    const std::vector<unsigned char> digest =
        SwCrypto::generateHashSHA256(std::string(bytes.constData(), bytes.size()));
    static const char* kHex = "0123456789abcdef";
    SwString out;
    out.reserve(digest.size() * 2);
    for (size_t i = 0; i < digest.size(); ++i) {
        out += kHex[(digest[i] >> 4) & 0x0F];
        out += kHex[digest[i] & 0x0F];
    }
    return out;
}

static const swinstaller::SwInstallerEmbeddedPayload& selfTestPayload() {
    using namespace swinstaller;
    static const unsigned char kHello[] = { 'h', 'e', 'l', 'l', 'o', '\n' };
    static SwInstallerEmbeddedPayload payload = []() {
        SwInstallerEmbeddedPayload p;
        p.payloadId = "SwInstallerFrameworkSelfTestPayload";
        p.displayName = "SwInstallerFrameworkSelfTestPayload";

        SwInstallerEmbeddedFile file;
        file.relativePath = "bin/hello.txt";
        file.originalSize = 6;
        file.storedSize = 6;
        file.bytes = kHello;
        file.checksumSha256 = sha256Hex(SwByteArray(reinterpret_cast<const char*>(kHello), 6));
        p.files.append(file);
        return p;
    }();
    return payload;
}

static swinstaller::SwInstallerProduct selfTestProduct() {
    using namespace swinstaller;

    SwInstallerProduct product;
    product.setProductId("SwInstallerFrameworkSelfTest")
        .setDisplayName("SwInstallerFrameworkSelfTest")
        .setPublisher("Ariya Consulting")
        .setVersion("1.0.0")
        .setMainExecutableRelativePath("bin/hello.txt")
        .setDisplayIconRelativePath("bin/hello.txt")
        .setCachedSetupFileName("SwInstallerFrameworkSelfTest.exe");

    SwInstallerComponent component("core", "Core");
    component.setDescription("Minimal runtime payload and JSON output for installer tests.");
    component.addPayloadTree(&selfTestPayload());

    SwInstallerWriteJsonSpec jsonFile;
    jsonFile.targetRelativePath = "config/setup.json";
    jsonFile.document["message"] = "hello";
    jsonFile.document["ok"] = true;
    component.addJsonFile(jsonFile);

    product.addComponent(component);
    return product;
}

static bool fail(const SwString& message) {
    std::cerr << message.toStdString() << "\n";
    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    SwString tempBase = SwDir::normalizePath(
        SwString(std::filesystem::current_path().string()) + "/tmp");
    const SwString tempRoot = SwDir::normalizePath(tempBase + "/SwInstallerFrameworkSelfTest");
    (void)SwDir::removeRecursively(tempRoot);
    std::error_code ec;
    std::filesystem::create_directories(tempRoot.toStdString(), ec);
    if (ec || !SwDir::exists(tempRoot)) {
        return fail("failed to create self-test temp root") ? 0 : 1;
    }

    const SwString installRoot = SwDir::normalizePath(tempRoot + "/install");
    const SwString stateRoot = SwDir::normalizePath(tempRoot + "/state");

    const swinstaller::SwInstallerProduct product = selfTestProduct();
    swinstaller::SwInstallerEngine engine(product, swinstaller::SwInstallerWindows::currentExecutablePath());

    swinstaller::SwInstallerPlan plan = engine.planInstall(installRoot, SwList<SwString>(), false);
    plan.stateRoot = stateRoot;
    plan.stateFilePath = SwDir::normalizePath(stateRoot + "/installer-state.json");
    plan.cachedSetupPath = SwDir::normalizePath(stateRoot + "/Setup.exe");
    plan.uninstallRegistryKeyPath.clear();

    SwList<swinstaller::SwInstallerPlanAction> filtered;
    for (size_t i = 0; i < plan.actions.size(); ++i) {
        const swinstaller::SwInstallerPlanAction& action = plan.actions[i];
        if (action.kind == swinstaller::SwInstallerActionKind::RegisterUninstall ||
            action.kind == swinstaller::SwInstallerActionKind::CopySetupSelf ||
            action.kind == swinstaller::SwInstallerActionKind::CreateShortcut ||
            action.kind == swinstaller::SwInstallerActionKind::RunPrerequisite) {
            continue;
        }
        filtered.append(action);
    }
    plan.actions = filtered;

    SwString parseErr;
    const swinstaller::SwInstallerPlan roundTrip =
        swinstaller::SwInstallerPlan::fromJson(plan.toJson(), &parseErr);
    if (!parseErr.isEmpty()) {
        (void)SwDir::removeRecursively(tempRoot);
        return fail(parseErr) ? 0 : 1;
    }
    if (roundTrip.actions.size() != plan.actions.size()) {
        (void)SwDir::removeRecursively(tempRoot);
        return fail("plan serialization roundtrip changed the action count") ? 0 : 1;
    }

    SwString execErr;
    const swinstaller::SwInstallerExecutionResult result = engine.execute(plan, false, &execErr);
    if (!result.ok) {
        (void)SwDir::removeRecursively(tempRoot);
        return fail(result.message.isEmpty() ? execErr : result.message) ? 0 : 1;
    }

    const SwString installedFile = SwDir::normalizePath(installRoot + "/bin/hello.txt");
    if (!SwFileInfo(installedFile.toStdString()).exists()) {
        (void)SwDir::removeRecursively(tempRoot);
        return fail("payload extraction did not create bin/hello.txt") ? 0 : 1;
    }

    SwFile hello(installedFile);
    if (!hello.open(SwFile::Read)) {
        (void)SwDir::removeRecursively(tempRoot);
        return fail("failed to read installed payload file") ? 0 : 1;
    }
    const SwString helloText = hello.readAll();
    hello.close();
    if (helloText != "hello\n") {
        (void)SwDir::removeRecursively(tempRoot);
        return fail("installed payload content mismatch") ? 0 : 1;
    }

    const SwString jsonFile = SwDir::normalizePath(installRoot + "/config/setup.json");
    if (!SwFileInfo(jsonFile.toStdString()).exists()) {
        (void)SwDir::removeRecursively(tempRoot);
        return fail("writeJson action did not create config/setup.json") ? 0 : 1;
    }
    if (!SwFileInfo(plan.stateFilePath.toStdString()).exists()) {
        (void)SwDir::removeRecursively(tempRoot);
        return fail("installer state file was not written") ? 0 : 1;
    }

    std::cout << "SwInstallerFrameworkSelfTest passed\n";
    (void)SwDir::removeRecursively(tempRoot);
    return 0;
}
