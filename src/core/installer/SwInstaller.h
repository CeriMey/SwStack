#pragma once

#include "SwInstallerPayload.h"
#include "SwInstallerWindows.h"

#include "SwDir.h"
#include "SwFile.h"
#include "SwFileInfo.h"
#include "SwJsonArray.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwStandardPaths.h"
#include "SwString.h"

#include <cstdio>
#include <string>

namespace swinstaller {

enum class SwInstallerShortcutLocation {
    Desktop,
    StartMenu,
    Both
};

enum class SwInstallerPlanKind {
    Install,
    Repair,
    Uninstall
};

enum class SwInstallerActionKind {
    ExtractPayloadFile,
    WriteJsonFile,
    RunPrerequisite,
    CopySetupSelf,
    CreateShortcut,
    RegisterUninstall,
    RemoveOwnedFile,
    RemoveOwnedDirectory,
    RemoveShortcut,
    RemoveUninstallRegistration,
    RemoveStateFile
};

struct SwInstallerPayloadFileSpec {
    const SwInstallerEmbeddedPayload* payload{nullptr};
    SwString relativePath;
};

struct SwInstallerShortcutSpec {
    SwString linkName;
    SwString targetRelativePath;
    SwString arguments;
    SwString workingDirectoryRelativePath;
    SwString description;
    SwString iconRelativePath;
    int iconIndex{0};
    SwInstallerShortcutLocation location{SwInstallerShortcutLocation::StartMenu};
};

struct SwInstallerPrerequisiteSpec {
    SwString displayName;
    SwString targetRelativePath;
    SwString arguments;
    bool createNoWindow{true};
    long long expectedExitCode{0};
    bool optional{false};
};

struct SwInstallerWriteJsonSpec {
    SwString targetRelativePath;
    SwJsonObject document;
    bool overwrite{true};
    bool owned{true};
    bool preserveIfModified{false};
};

struct SwInstallerLaunchSpec {
    SwString displayName;
    SwString targetRelativePath;
    SwString arguments;
    SwString workingDirectoryRelativePath;
};

class SwInstallerComponent {
public:
    SwString componentId;
    SwString displayName;
    SwString description;
    bool selectedByDefault{true};
    SwList<const SwInstallerEmbeddedPayload*> payloadTrees;
    SwList<SwInstallerPayloadFileSpec> payloadFiles;
    SwList<SwInstallerShortcutSpec> shortcuts;
    SwList<SwInstallerPrerequisiteSpec> prerequisites;
    SwList<SwInstallerWriteJsonSpec> jsonFiles;
    SwList<SwInstallerLaunchSpec> finalLaunches;

    SwInstallerComponent() = default;

    explicit SwInstallerComponent(const SwString& id, const SwString& title = SwString())
        : componentId(id), displayName(title.isEmpty() ? id : title) {}

    SwInstallerComponent& setDescription(const SwString& value) {
        description = value;
        return *this;
    }

    SwInstallerComponent& setSelectedByDefault(bool value) {
        selectedByDefault = value;
        return *this;
    }

    SwInstallerComponent& addPayloadTree(const SwInstallerEmbeddedPayload* payload) {
        if (payload && !payloadTrees.contains(payload)) {
            payloadTrees.append(payload);
        }
        return *this;
    }

    SwInstallerComponent& addPayloadFile(const SwInstallerEmbeddedPayload* payload,
                                         const SwString& relativePath) {
        if (!payload || relativePath.isEmpty()) {
            return *this;
        }
        SwInstallerPayloadFileSpec spec;
        spec.payload = payload;
        spec.relativePath = SwInstallerPayload::normalizeRelativePath(relativePath);
        payloadFiles.append(spec);
        return *this;
    }

    SwInstallerComponent& addShortcut(const SwInstallerShortcutSpec& shortcut) {
        shortcuts.append(shortcut);
        return *this;
    }

    SwInstallerComponent& addPrerequisite(const SwInstallerPrerequisiteSpec& prerequisite) {
        prerequisites.append(prerequisite);
        return *this;
    }

    SwInstallerComponent& addJsonFile(const SwInstallerWriteJsonSpec& jsonFile) {
        jsonFiles.append(jsonFile);
        return *this;
    }

    SwInstallerComponent& addFinalLaunch(const SwInstallerLaunchSpec& launch) {
        finalLaunches.append(launch);
        return *this;
    }
};

class SwInstallerProduct {
public:
    SwString productId;
    SwString displayName;
    SwString publisher;
    SwString version;
    SwString mainExecutableRelativePath;
    SwString displayIconRelativePath;
    SwString defaultInstallRootOverride;
    SwString cachedSetupFileName{"Setup.exe"};
    bool machineWideInstall{false};
    SwList<SwInstallerComponent> components;

    SwInstallerProduct& setProductId(const SwString& value) { productId = value; return *this; }
    SwInstallerProduct& setDisplayName(const SwString& value) { displayName = value; return *this; }
    SwInstallerProduct& setPublisher(const SwString& value) { publisher = value; return *this; }
    SwInstallerProduct& setVersion(const SwString& value) { version = value; return *this; }
    SwInstallerProduct& setMainExecutableRelativePath(const SwString& value) {
        mainExecutableRelativePath = SwInstallerPayload::normalizeRelativePath(value);
        return *this;
    }
    SwInstallerProduct& setDisplayIconRelativePath(const SwString& value) {
        displayIconRelativePath = SwInstallerPayload::normalizeRelativePath(value);
        return *this;
    }
    SwInstallerProduct& setDefaultInstallRoot(const SwString& value) {
        defaultInstallRootOverride = swInstallerNormalizePath(value);
        return *this;
    }
    SwInstallerProduct& setCachedSetupFileName(const SwString& value) {
        cachedSetupFileName = value;
        return *this;
    }
    SwInstallerProduct& addComponent(const SwInstallerComponent& component) {
        components.append(component);
        return *this;
    }

    SwString effectiveDisplayName() const {
        return displayName.isEmpty() ? productId : displayName;
    }

    SwString resolveDefaultInstallRoot() const {
        if (!defaultInstallRootOverride.isEmpty()) {
            return swInstallerNormalizePath(defaultInstallRootOverride);
        }
        SwString base = SwStandardPaths::writableLocation(SwStandardPaths::AppDataLocation);
        if (base.isEmpty()) {
            base = SwStandardPaths::writableLocation(SwStandardPaths::AppLocalDataLocation);
        }
        if (base.isEmpty()) {
            base = SwStandardPaths::writableLocation(SwStandardPaths::HomeLocation);
        }
        return swInstallerNormalizePath(base + "/" + sanitizeLeaf_(publisher.isEmpty() ? SwString("SwInstaller") : publisher) +
                                        "/" + sanitizeLeaf_(effectiveDisplayName()));
    }

    SwString resolveStateRoot() const {
        SwString base = SwStandardPaths::writableLocation(SwStandardPaths::AppDataLocation);
        if (base.isEmpty()) {
            base = SwStandardPaths::writableLocation(SwStandardPaths::AppLocalDataLocation);
        }
        if (base.isEmpty()) {
            base = SwStandardPaths::writableLocation(SwStandardPaths::HomeLocation);
        }
        return swInstallerNormalizePath(base + "/" + sanitizeLeaf_(publisher.isEmpty() ? SwString("SwInstaller") : publisher) +
                                        "/" + sanitizeLeaf_(effectiveDisplayName()));
    }

    SwString resolveStateFilePath() const {
        return swInstallerNormalizePath(resolveStateRoot() + "/installer-state.json");
    }

    SwString resolveCachedSetupPath() const {
        return swInstallerNormalizePath(resolveStateRoot() + "/" + cachedSetupFileName);
    }

    const SwInstallerComponent* findComponent(const SwString& componentIdValue) const {
        for (size_t i = 0; i < components.size(); ++i) {
            if (components[i].componentId == componentIdValue) {
                return &components[i];
            }
        }
        return nullptr;
    }

    const SwInstallerEmbeddedPayload* findPayload(const SwString& payloadIdValue) const {
        for (size_t i = 0; i < components.size(); ++i) {
            const SwInstallerComponent& component = components[i];
            for (size_t k = 0; k < component.payloadTrees.size(); ++k) {
                if (component.payloadTrees[k] && component.payloadTrees[k]->payloadId == payloadIdValue) {
                    return component.payloadTrees[k];
                }
            }
            for (size_t k = 0; k < component.payloadFiles.size(); ++k) {
                if (component.payloadFiles[k].payload &&
                    component.payloadFiles[k].payload->payloadId == payloadIdValue) {
                    return component.payloadFiles[k].payload;
                }
            }
        }
        return nullptr;
    }

private:
    static SwString sanitizeLeaf_(SwString value) {
        value.replace("\\", "_");
        value.replace("/", "_");
        value.replace(":", "_");
        return value;
    }
};

struct SwInstallerPlanAction {
    SwInstallerActionKind kind{SwInstallerActionKind::ExtractPayloadFile};
    SwString title;
    SwString payloadId;
    SwString payloadRelativePath;
    SwString sourcePath;
    SwString targetPath;
    SwString arguments;
    SwString workingDirectory;
    SwString linkPath;
    SwString description;
    SwString iconPath;
    int iconIndex{0};
    SwString registryKeyPath;
    SwString checksumSha256;
    bool overwrite{true};
    bool preserveIfModified{false};
    bool createNoWindow{true};
    long long expectedExitCode{0};
    bool optional{false};
    SwJsonObject jsonDocument;

    static SwString kindToString(SwInstallerActionKind kindValue) {
        switch (kindValue) {
        case SwInstallerActionKind::ExtractPayloadFile: return "extract_payload_file";
        case SwInstallerActionKind::WriteJsonFile: return "write_json_file";
        case SwInstallerActionKind::RunPrerequisite: return "run_prerequisite";
        case SwInstallerActionKind::CopySetupSelf: return "copy_setup_self";
        case SwInstallerActionKind::CreateShortcut: return "create_shortcut";
        case SwInstallerActionKind::RegisterUninstall: return "register_uninstall";
        case SwInstallerActionKind::RemoveOwnedFile: return "remove_owned_file";
        case SwInstallerActionKind::RemoveOwnedDirectory: return "remove_owned_directory";
        case SwInstallerActionKind::RemoveShortcut: return "remove_shortcut";
        case SwInstallerActionKind::RemoveUninstallRegistration: return "remove_uninstall_registration";
        case SwInstallerActionKind::RemoveStateFile: return "remove_state_file";
        default: return "extract_payload_file";
        }
    }

    static SwInstallerActionKind stringToKind(const SwString& value) {
        if (value == "write_json_file") return SwInstallerActionKind::WriteJsonFile;
        if (value == "run_prerequisite") return SwInstallerActionKind::RunPrerequisite;
        if (value == "copy_setup_self") return SwInstallerActionKind::CopySetupSelf;
        if (value == "create_shortcut") return SwInstallerActionKind::CreateShortcut;
        if (value == "register_uninstall") return SwInstallerActionKind::RegisterUninstall;
        if (value == "remove_owned_file") return SwInstallerActionKind::RemoveOwnedFile;
        if (value == "remove_owned_directory") return SwInstallerActionKind::RemoveOwnedDirectory;
        if (value == "remove_shortcut") return SwInstallerActionKind::RemoveShortcut;
        if (value == "remove_uninstall_registration") return SwInstallerActionKind::RemoveUninstallRegistration;
        if (value == "remove_state_file") return SwInstallerActionKind::RemoveStateFile;
        return SwInstallerActionKind::ExtractPayloadFile;
    }

    SwJsonObject toJson() const {
        SwJsonObject obj;
        obj["kind"] = kindToString(kind).toStdString();
        obj["title"] = title.toStdString();
        obj["payloadId"] = payloadId.toStdString();
        obj["payloadRelativePath"] = payloadRelativePath.toStdString();
        obj["sourcePath"] = sourcePath.toStdString();
        obj["targetPath"] = targetPath.toStdString();
        obj["arguments"] = arguments.toStdString();
        obj["workingDirectory"] = workingDirectory.toStdString();
        obj["linkPath"] = linkPath.toStdString();
        obj["description"] = description.toStdString();
        obj["iconPath"] = iconPath.toStdString();
        obj["iconIndex"] = iconIndex;
        obj["registryKeyPath"] = registryKeyPath.toStdString();
        obj["checksumSha256"] = checksumSha256.toStdString();
        obj["overwrite"] = overwrite;
        obj["preserveIfModified"] = preserveIfModified;
        obj["createNoWindow"] = createNoWindow;
        obj["expectedExitCode"] = expectedExitCode;
        obj["optional"] = optional;
        obj["jsonDocument"] = jsonDocument;
        return obj;
    }

    static SwInstallerPlanAction fromJson(const SwJsonObject& obj) {
        SwInstallerPlanAction action;
        action.kind = stringToKind(SwString(obj["kind"].toString()));
        action.title = SwString(obj["title"].toString());
        action.payloadId = SwString(obj["payloadId"].toString());
        action.payloadRelativePath = SwString(obj["payloadRelativePath"].toString());
        action.sourcePath = SwString(obj["sourcePath"].toString());
        action.targetPath = SwString(obj["targetPath"].toString());
        action.arguments = SwString(obj["arguments"].toString());
        action.workingDirectory = SwString(obj["workingDirectory"].toString());
        action.linkPath = SwString(obj["linkPath"].toString());
        action.description = SwString(obj["description"].toString());
        action.iconPath = SwString(obj["iconPath"].toString());
        action.iconIndex = obj["iconIndex"].toInt(0);
        action.registryKeyPath = SwString(obj["registryKeyPath"].toString());
        action.checksumSha256 = SwString(obj["checksumSha256"].toString());
        action.overwrite = obj["overwrite"].toBool(true);
        action.preserveIfModified = obj["preserveIfModified"].toBool(false);
        action.createNoWindow = obj["createNoWindow"].toBool(true);
        action.expectedExitCode = obj["expectedExitCode"].toLongLong();
        action.optional = obj["optional"].toBool(false);
        if (obj.contains("jsonDocument") && obj["jsonDocument"].isObject()) {
            action.jsonDocument = obj["jsonDocument"].toObject();
        }
        return action;
    }
};

struct SwInstallerPlan {
    SwInstallerPlanKind kind{SwInstallerPlanKind::Install};
    SwString productId;
    SwString displayName;
    SwString publisher;
    SwString version;
    SwString installRoot;
    SwString stateRoot;
    SwString stateFilePath;
    SwString setupSelfPath;
    SwString cachedSetupPath;
    SwString uninstallRegistryKeyPath;
    bool silent{false};
    SwList<SwString> selectedComponentIds;
    SwList<SwInstallerPlanAction> actions;
    SwList<SwInstallerLaunchSpec> finalLaunches;

    static SwString kindToString(SwInstallerPlanKind kindValue) {
        switch (kindValue) {
        case SwInstallerPlanKind::Install: return "install";
        case SwInstallerPlanKind::Repair: return "repair";
        case SwInstallerPlanKind::Uninstall: return "uninstall";
        default: return "install";
        }
    }

    static SwInstallerPlanKind stringToKind(const SwString& value) {
        if (value == "repair") return SwInstallerPlanKind::Repair;
        if (value == "uninstall") return SwInstallerPlanKind::Uninstall;
        return SwInstallerPlanKind::Install;
    }

    SwJsonObject toJsonObject() const;
    SwString toJson() const;
    static SwInstallerPlan fromJson(const SwString& jsonText, SwString* errOut = nullptr);
    static SwInstallerPlan fromJsonObject(const SwJsonObject& obj);
};

struct SwInstallerDetection {
    bool installed{false};
    SwString installRoot;
    SwString stateFilePath;
    SwJsonObject stateObject;
};

struct SwInstallerExecutionResult {
    bool ok{false};
    SwString message;
    SwJsonObject stateObject;

    SwString toJson() const {
        SwJsonObject obj;
        obj["ok"] = ok;
        obj["message"] = message.toStdString();
        obj["stateObject"] = stateObject;
        return SwJsonDocument(obj).toJson(SwJsonDocument::JsonFormat::Pretty);
    }

    static SwInstallerExecutionResult fromJson(const SwString& jsonText, SwString* errOut = nullptr) {
        SwInstallerExecutionResult result;
        SwString parseErr;
        const SwJsonDocument doc = SwJsonDocument::fromJson(jsonText.toStdString(), parseErr);
        if (!parseErr.isEmpty() || !doc.isObject()) {
            if (errOut) {
                *errOut = parseErr.isEmpty() ? SwString("result JSON root is not an object") : parseErr;
            }
            return result;
        }
        const SwJsonObject obj = doc.object();
        result.ok = obj["ok"].toBool(false);
        result.message = SwString(obj["message"].toString());
        if (obj.contains("stateObject") && obj["stateObject"].isObject()) {
            result.stateObject = obj["stateObject"].toObject();
        }
        return result;
    }
};

class SwInstallerEngine {
public:
    explicit SwInstallerEngine(const SwInstallerProduct& product,
                               const SwString& setupSelfPath = SwInstallerWindows::currentExecutablePath());

    const SwInstallerProduct& product() const { return product_; }
    SwInstallerDetection detect() const;
    SwInstallerPlan planInstall(const SwString& installRootOverride = SwString(),
                                const SwList<SwString>& selectedComponentIds = SwList<SwString>(),
                                bool silent = false) const;
    SwInstallerPlan planRepair(bool silent = false) const;
    SwInstallerPlan planUninstall(bool silent = false) const;
    SwInstallerExecutionResult execute(const SwInstallerPlan& plan,
                                       bool allowPostInstallLaunch = false,
                                       SwString* errOut = nullptr);
    bool rollback(SwString* errOut = nullptr);
    bool launchPostInstallActions(const SwInstallerPlan& plan, SwString* errOut = nullptr) const;

private:
    struct RollbackState_ {
        SwList<SwString> files;
        SwList<SwString> directories;
        SwList<SwString> shortcuts;
        SwList<SwString> registryKeys;
        SwString stateFilePath;
    };

    SwInstallerProduct product_;
    SwString setupSelfPath_;
    RollbackState_ rollbackState_;

    void initializePlanHeader_(SwInstallerPlan& plan, const SwString& installRootOverride, bool silent) const;
    SwList<SwString> resolveSelectedComponents_(const SwList<SwString>& requestedIds) const;
    SwList<SwInstallerLaunchSpec> collectFinalLaunches_(const SwList<SwString>& selectedIds) const;
    void appendInstallActionsForComponent_(const SwInstallerComponent& component,
                                           SwInstallerPlan& plan,
                                           SwList<SwString>& seenPayloadKeys) const;
    void appendPayloadAction_(const SwInstallerEmbeddedPayload* payload,
                              const SwString& relativePath,
                              SwInstallerPlan& plan,
                              SwList<SwString>& seenPayloadKeys) const;
    void appendShortcutAction_(const SwInstallerShortcutSpec& shortcut, SwInstallerPlan& plan) const;
    void appendUninstallActions_(const SwJsonObject& state, SwInstallerPlan& plan) const;

    bool executeAction_(const SwInstallerPlan& plan,
                        const SwInstallerPlanAction& action,
                        SwJsonObject& state,
                        SwString* errOut);
    bool executeExtractPayload_(const SwInstallerPlanAction& action,
                                const SwInstallerPlan& plan,
                                SwJsonObject& state,
                                SwString* errOut);
    bool executeWriteJson_(const SwInstallerPlanAction& action,
                           const SwInstallerPlan& plan,
                           SwJsonObject& state,
                           SwString* errOut);
    bool executePrerequisite_(const SwInstallerPlanAction& action, SwString* errOut);
    bool executeCopySetup_(const SwInstallerPlanAction& action,
                           const SwInstallerPlan& plan,
                           SwJsonObject& state,
                           SwString* errOut);
    bool executeCreateShortcut_(const SwInstallerPlanAction& action,
                                SwJsonObject& state,
                                SwString* errOut);
    bool executeRegisterUninstall_(const SwInstallerPlanAction& action,
                                   const SwInstallerPlan& plan,
                                   SwJsonObject& state,
                                   SwString* errOut);
    bool executeRemoveOwnedFile_(const SwInstallerPlanAction& action, SwString* errOut);
    bool executeRemoveOwnedDirectory_(const SwInstallerPlanAction& action, SwString* errOut);
    bool executeRemoveShortcut_(const SwInstallerPlanAction& action, SwString* errOut);
    bool executeRemoveUninstallRegistration_(const SwInstallerPlanAction& action, SwString* errOut);
    bool executeRemoveStateFile_(const SwInstallerPlanAction& action, SwString* errOut);

    SwJsonObject initialStateObject_(const SwInstallerPlan& plan) const;
    static bool saveStateObject_(const SwString& path, const SwJsonObject& state, SwString* errOut);
    static SwJsonObject loadStateObject_(const SwString& path, SwString* errOut);

    static void appendOwnedFile_(SwJsonObject& state, const SwString& path, const SwString& checksum, bool preserveIfModified);
    static void collectOwnedDirectoriesFromPath_(SwJsonObject& state, SwString path, const SwString& stopRoot);
    static void appendUniqueStringArray_(SwJsonObject& state, const SwString& key, const SwString& value);

    void appendRollbackFile_(const SwString& path);
    void appendRollbackDirectoriesFromPath_(SwString path, const SwString& stopRoot);
    void clearRollback_();

    static void appendSortedByDepth_(const SwList<SwInstallerPlanAction>& input, SwList<SwInstallerPlanAction>& output);
    static void removeDirectoriesReverse_(const SwList<SwString>& dirs);
    static SwString parentPath_(SwString path);
    static SwString expandTokens_(SwString value, const SwString& installRoot, const SwString& stateRoot);
    SwString displayIconAbsolute_(const SwString& installRoot) const;
    static SwList<SwString> splitCommandLine_(const SwString& text);
    static SwString checksumForFile_(const SwString& path);
    static bool replaceFile_(const SwString& tempPath, const SwString& destinationPath, SwString* errOut);
    static bool launchDetached_(const SwString& executablePath,
                                const SwList<SwString>& arguments,
                                const SwString& workingDirectory,
                                SwString* errOut);
    static std::wstring quoteArgument_(const std::wstring& s);
};

inline SwJsonObject SwInstallerPlan::toJsonObject() const {
    SwJsonObject obj;
    obj["kind"] = kindToString(kind).toStdString();
    obj["productId"] = productId.toStdString();
    obj["displayName"] = displayName.toStdString();
    obj["publisher"] = publisher.toStdString();
    obj["version"] = version.toStdString();
    obj["installRoot"] = installRoot.toStdString();
    obj["stateRoot"] = stateRoot.toStdString();
    obj["stateFilePath"] = stateFilePath.toStdString();
    obj["setupSelfPath"] = setupSelfPath.toStdString();
    obj["cachedSetupPath"] = cachedSetupPath.toStdString();
    obj["uninstallRegistryKeyPath"] = uninstallRegistryKeyPath.toStdString();
    obj["silent"] = silent;

    SwJsonArray componentsArr;
    for (size_t i = 0; i < selectedComponentIds.size(); ++i) {
        componentsArr.append(selectedComponentIds[i].toStdString());
    }
    obj["selectedComponentIds"] = componentsArr;

    SwJsonArray actionsArr;
    for (size_t i = 0; i < actions.size(); ++i) {
        actionsArr.append(actions[i].toJson());
    }
    obj["actions"] = actionsArr;

    SwJsonArray launchesArr;
    for (size_t i = 0; i < finalLaunches.size(); ++i) {
        SwJsonObject launchObj;
        launchObj["displayName"] = finalLaunches[i].displayName.toStdString();
        launchObj["targetRelativePath"] = finalLaunches[i].targetRelativePath.toStdString();
        launchObj["arguments"] = finalLaunches[i].arguments.toStdString();
        launchObj["workingDirectoryRelativePath"] =
            finalLaunches[i].workingDirectoryRelativePath.toStdString();
        launchesArr.append(launchObj);
    }
    obj["finalLaunches"] = launchesArr;
    return obj;
}

inline SwString SwInstallerPlan::toJson() const {
    return SwJsonDocument(toJsonObject()).toJson(SwJsonDocument::JsonFormat::Pretty);
}

inline SwInstallerPlan SwInstallerPlan::fromJson(const SwString& jsonText, SwString* errOut) {
    SwInstallerPlan plan;
    SwString parseErr;
    const SwJsonDocument doc = SwJsonDocument::fromJson(jsonText.toStdString(), parseErr);
    if (!parseErr.isEmpty() || !doc.isObject()) {
        if (errOut) {
            *errOut = parseErr.isEmpty() ? SwString("plan JSON root is not an object") : parseErr;
        }
        return plan;
    }
    if (errOut) {
        errOut->clear();
    }
    return fromJsonObject(doc.object());
}

inline SwInstallerPlan SwInstallerPlan::fromJsonObject(const SwJsonObject& obj) {
    SwInstallerPlan plan;
    plan.kind = stringToKind(SwString(obj["kind"].toString()));
    plan.productId = SwString(obj["productId"].toString());
    plan.displayName = SwString(obj["displayName"].toString());
    plan.publisher = SwString(obj["publisher"].toString());
    plan.version = SwString(obj["version"].toString());
    plan.installRoot = SwString(obj["installRoot"].toString());
    plan.stateRoot = SwString(obj["stateRoot"].toString());
    plan.stateFilePath = SwString(obj["stateFilePath"].toString());
    plan.setupSelfPath = SwString(obj["setupSelfPath"].toString());
    plan.cachedSetupPath = SwString(obj["cachedSetupPath"].toString());
    plan.uninstallRegistryKeyPath = SwString(obj["uninstallRegistryKeyPath"].toString());
    plan.silent = obj["silent"].toBool(false);

    if (obj.contains("selectedComponentIds") && obj["selectedComponentIds"].isArray()) {
        const SwJsonArray arr = obj["selectedComponentIds"].toArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            plan.selectedComponentIds.append(SwString(arr[i].toString()));
        }
    }
    if (obj.contains("actions") && obj["actions"].isArray()) {
        const SwJsonArray arr = obj["actions"].toArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            if (arr[i].isObject()) {
                plan.actions.append(SwInstallerPlanAction::fromJson(arr[i].toObject()));
            }
        }
    }
    if (obj.contains("finalLaunches") && obj["finalLaunches"].isArray()) {
        const SwJsonArray arr = obj["finalLaunches"].toArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            if (!arr[i].isObject()) continue;
            const SwJsonObject launchObj = arr[i].toObject();
            SwInstallerLaunchSpec launch;
            launch.displayName = SwString(launchObj["displayName"].toString());
            launch.targetRelativePath = SwString(launchObj["targetRelativePath"].toString());
            launch.arguments = SwString(launchObj["arguments"].toString());
            launch.workingDirectoryRelativePath =
                SwString(launchObj["workingDirectoryRelativePath"].toString());
            plan.finalLaunches.append(launch);
        }
    }
    return plan;
}

inline SwInstallerEngine::SwInstallerEngine(const SwInstallerProduct& product,
                                            const SwString& setupSelfPath)
    : product_(product), setupSelfPath_(setupSelfPath) {}

inline SwInstallerDetection SwInstallerEngine::detect() const {
    SwInstallerDetection detection;
    detection.stateFilePath = product_.resolveStateFilePath();
    SwString err;
    detection.stateObject = loadStateObject_(detection.stateFilePath, &err);
    if (!err.isEmpty() || detection.stateObject.isEmpty()) {
        return detection;
    }
    detection.installRoot = swInstallerNormalizePath(SwString(detection.stateObject["installRoot"].toString()));
    if (!detection.installRoot.isEmpty()) {
        const SwString mainPath = SwInstallerPayload::joinRootAndRelative(
            detection.installRoot,
            product_.mainExecutableRelativePath);
        detection.installed = SwFile::exists(mainPath);
    }
    return detection;
}

inline void SwInstallerEngine::initializePlanHeader_(SwInstallerPlan& plan,
                                                     const SwString& installRootOverride,
                                                     bool silent) const {
    plan.productId = product_.productId;
    plan.displayName = product_.effectiveDisplayName();
    plan.publisher = product_.publisher;
    plan.version = product_.version;
    plan.installRoot = installRootOverride.isEmpty() ? product_.resolveDefaultInstallRoot()
                                                     : swInstallerNormalizePath(installRootOverride);
    plan.stateRoot = product_.resolveStateRoot();
    plan.stateFilePath = product_.resolveStateFilePath();
    plan.setupSelfPath = setupSelfPath_;
    plan.cachedSetupPath = product_.resolveCachedSetupPath();
    plan.uninstallRegistryKeyPath = SwInstallerWindows::makeUninstallRegistryKey(product_.productId);
    plan.silent = silent;
}

inline SwList<SwString> SwInstallerEngine::resolveSelectedComponents_(const SwList<SwString>& requestedIds) const {
    if (!requestedIds.isEmpty()) {
        return requestedIds;
    }

    SwList<SwString> selected;
    for (size_t i = 0; i < product_.components.size(); ++i) {
        if (product_.components[i].selectedByDefault) {
            selected.append(product_.components[i].componentId);
        }
    }
    if (selected.isEmpty()) {
        for (size_t i = 0; i < product_.components.size(); ++i) {
            selected.append(product_.components[i].componentId);
        }
    }
    return selected;
}

inline SwList<SwInstallerLaunchSpec> SwInstallerEngine::collectFinalLaunches_(const SwList<SwString>& selectedIds) const {
    SwList<SwInstallerLaunchSpec> launches;
    for (size_t i = 0; i < selectedIds.size(); ++i) {
        const SwInstallerComponent* component = product_.findComponent(selectedIds[i]);
        if (!component) {
            continue;
        }
        launches.append(component->finalLaunches);
    }
    return launches;
}

inline SwInstallerPlan SwInstallerEngine::planInstall(const SwString& installRootOverride,
                                                      const SwList<SwString>& selectedComponentIds,
                                                      bool silent) const {
    SwInstallerPlan plan;
    plan.kind = detect().installed ? SwInstallerPlanKind::Repair : SwInstallerPlanKind::Install;
    initializePlanHeader_(plan, installRootOverride, silent);
    plan.selectedComponentIds = resolveSelectedComponents_(selectedComponentIds);

    SwList<SwString> seenPayloadKeys;
    for (size_t i = 0; i < plan.selectedComponentIds.size(); ++i) {
        const SwInstallerComponent* component = product_.findComponent(plan.selectedComponentIds[i]);
        if (!component) {
            continue;
        }
        appendInstallActionsForComponent_(*component, plan, seenPayloadKeys);
    }

    plan.finalLaunches = collectFinalLaunches_(plan.selectedComponentIds);

    SwInstallerPlanAction copySelf;
    copySelf.kind = SwInstallerActionKind::CopySetupSelf;
    copySelf.title = "Cache setup executable";
    copySelf.sourcePath = setupSelfPath_;
    copySelf.targetPath = plan.cachedSetupPath;
    plan.actions.append(copySelf);

    SwInstallerPlanAction reg;
    reg.kind = SwInstallerActionKind::RegisterUninstall;
    reg.title = "Register uninstall entry";
    reg.targetPath = plan.installRoot;
    reg.sourcePath = displayIconAbsolute_(plan.installRoot);
    reg.registryKeyPath = plan.uninstallRegistryKeyPath;
    reg.arguments = SwString("\"") + plan.cachedSetupPath + "\" --mode=uninstall --silent";
    plan.actions.append(reg);

    return plan;
}

inline SwInstallerPlan SwInstallerEngine::planRepair(bool silent) const {
    const SwInstallerDetection detection = detect();
    return planInstall(detection.installRoot, SwList<SwString>(), silent);
}

inline SwInstallerPlan SwInstallerEngine::planUninstall(bool silent) const {
    SwInstallerPlan plan;
    plan.kind = SwInstallerPlanKind::Uninstall;
    initializePlanHeader_(plan, detect().installRoot, silent);
    const SwInstallerDetection detection = detect();
    plan.installRoot = detection.installRoot.isEmpty() ? plan.installRoot : detection.installRoot;
    plan.stateFilePath = product_.resolveStateFilePath();
    plan.cachedSetupPath = product_.resolveCachedSetupPath();
    if (!detection.stateObject.isEmpty()) {
        appendUninstallActions_(detection.stateObject, plan);
    }
    return plan;
}

inline void SwInstallerEngine::appendInstallActionsForComponent_(const SwInstallerComponent& component,
                                                                 SwInstallerPlan& plan,
                                                                 SwList<SwString>& seenPayloadKeys) const {
    for (size_t i = 0; i < component.payloadTrees.size(); ++i) {
        const SwInstallerEmbeddedPayload* payload = component.payloadTrees[i];
        if (!payload) continue;
        for (size_t k = 0; k < payload->files.size(); ++k) {
            appendPayloadAction_(payload, payload->files[k].relativePath, plan, seenPayloadKeys);
        }
    }

    for (size_t i = 0; i < component.payloadFiles.size(); ++i) {
        if (!component.payloadFiles[i].payload) continue;
        appendPayloadAction_(component.payloadFiles[i].payload,
                             component.payloadFiles[i].relativePath,
                             plan,
                             seenPayloadKeys);
    }

    for (size_t i = 0; i < component.jsonFiles.size(); ++i) {
        SwInstallerPlanAction action;
        action.kind = SwInstallerActionKind::WriteJsonFile;
        action.title = component.displayName.isEmpty() ? component.componentId : component.displayName;
        action.targetPath = SwInstallerPayload::joinRootAndRelative(plan.installRoot,
                                                                    component.jsonFiles[i].targetRelativePath);
        action.jsonDocument = component.jsonFiles[i].document;
        action.overwrite = component.jsonFiles[i].overwrite;
        action.preserveIfModified = component.jsonFiles[i].preserveIfModified;
        plan.actions.append(action);
    }

    for (size_t i = 0; i < component.prerequisites.size(); ++i) {
        SwInstallerPlanAction action;
        action.kind = SwInstallerActionKind::RunPrerequisite;
        action.title = component.prerequisites[i].displayName;
        action.targetPath = SwInstallerPayload::joinRootAndRelative(plan.installRoot,
                                                                    component.prerequisites[i].targetRelativePath);
        action.arguments = expandTokens_(component.prerequisites[i].arguments, plan.installRoot, plan.stateRoot);
        action.workingDirectory = parentPath_(action.targetPath);
        action.createNoWindow = component.prerequisites[i].createNoWindow;
        action.expectedExitCode = component.prerequisites[i].expectedExitCode;
        action.optional = component.prerequisites[i].optional;
        plan.actions.append(action);
    }

    for (size_t i = 0; i < component.shortcuts.size(); ++i) {
        appendShortcutAction_(component.shortcuts[i], plan);
    }
}

inline void SwInstallerEngine::appendPayloadAction_(const SwInstallerEmbeddedPayload* payload,
                                                    const SwString& relativePath,
                                                    SwInstallerPlan& plan,
                                                    SwList<SwString>& seenPayloadKeys) const {
    if (!payload) {
        return;
    }
    const SwString normalizedRelative = SwInstallerPayload::normalizeRelativePath(relativePath);
    const SwString dedupeKey = payload->payloadId + "|" + normalizedRelative;
    if (seenPayloadKeys.contains(dedupeKey)) {
        return;
    }
    seenPayloadKeys.append(dedupeKey);

    const SwInstallerEmbeddedFile* embedded = payload->findFile(normalizedRelative);
    if (!embedded) {
        return;
    }

    SwInstallerPlanAction action;
    action.kind = SwInstallerActionKind::ExtractPayloadFile;
    action.title = normalizedRelative;
    action.payloadId = payload->payloadId;
    action.payloadRelativePath = normalizedRelative;
    action.targetPath = SwInstallerPayload::joinRootAndRelative(plan.installRoot, normalizedRelative);
    action.checksumSha256 = embedded->checksumSha256;
    plan.actions.append(action);
}

inline void SwInstallerEngine::appendShortcutAction_(const SwInstallerShortcutSpec& shortcut,
                                                     SwInstallerPlan& plan) const {
    const SwString args = expandTokens_(shortcut.arguments, plan.installRoot, plan.stateRoot);
    const SwString targetPath = SwInstallerPayload::joinRootAndRelative(plan.installRoot,
                                                                        shortcut.targetRelativePath);
    const SwString workingDir =
        shortcut.workingDirectoryRelativePath.isEmpty()
            ? parentPath_(targetPath)
            : SwInstallerPayload::joinRootAndRelative(
                  plan.installRoot,
                  expandTokens_(shortcut.workingDirectoryRelativePath, plan.installRoot, plan.stateRoot));
    const SwString iconPath =
        shortcut.iconRelativePath.isEmpty()
            ? displayIconAbsolute_(plan.installRoot)
            : SwInstallerPayload::joinRootAndRelative(plan.installRoot, shortcut.iconRelativePath);

    if (shortcut.location == SwInstallerShortcutLocation::Desktop ||
        shortcut.location == SwInstallerShortcutLocation::Both) {
        SwInstallerPlanAction action;
        action.kind = SwInstallerActionKind::CreateShortcut;
        action.title = shortcut.linkName;
        action.sourcePath = targetPath;
        action.arguments = args;
        action.workingDirectory = workingDir;
        action.description = shortcut.description;
        action.iconPath = iconPath;
        action.iconIndex = shortcut.iconIndex;
        action.linkPath = SwDir::normalizePath(
            (SwInstallerWindows::isProcessElevated()
                 ? SwInstallerWindows::commonDesktopDir()
                 : SwInstallerWindows::userDesktopDir()) +
            "/" + shortcut.linkName + ".lnk");
        plan.actions.append(action);
    }

    if (shortcut.location == SwInstallerShortcutLocation::StartMenu ||
        shortcut.location == SwInstallerShortcutLocation::Both) {
        SwInstallerPlanAction action;
        action.kind = SwInstallerActionKind::CreateShortcut;
        action.title = shortcut.linkName;
        action.sourcePath = targetPath;
        action.arguments = args;
        action.workingDirectory = workingDir;
        action.description = shortcut.description;
        action.iconPath = iconPath;
        action.iconIndex = shortcut.iconIndex;
        action.linkPath = SwDir::normalizePath(
            (SwInstallerWindows::isProcessElevated()
                 ? SwInstallerWindows::commonProgramsDir()
                 : SwInstallerWindows::userProgramsDir()) +
            "/" + product_.effectiveDisplayName() + "/" +
            shortcut.linkName + ".lnk");
        plan.actions.append(action);
    }
}

inline void SwInstallerEngine::appendUninstallActions_(const SwJsonObject& state, SwInstallerPlan& plan) const {
    if (state.contains("shortcuts") && state["shortcuts"].isArray()) {
        const SwJsonArray arr = state["shortcuts"].toArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            SwInstallerPlanAction action;
            action.kind = SwInstallerActionKind::RemoveShortcut;
            action.linkPath = SwString(arr[i].toString());
            action.title = action.linkPath;
            plan.actions.append(action);
        }
    }

    if (state.contains("registryKeys") && state["registryKeys"].isArray()) {
        const SwJsonArray arr = state["registryKeys"].toArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            SwInstallerPlanAction action;
            action.kind = SwInstallerActionKind::RemoveUninstallRegistration;
            action.registryKeyPath = SwString(arr[i].toString());
            action.title = action.registryKeyPath;
            plan.actions.append(action);
        }
    }

    if (state.contains("ownedFiles") && state["ownedFiles"].isArray()) {
        const SwJsonArray arr = state["ownedFiles"].toArray();
        SwList<SwInstallerPlanAction> fileActions;
        for (size_t i = 0; i < arr.size(); ++i) {
            if (!arr[i].isObject()) continue;
            const SwJsonObject fileObj = arr[i].toObject();
            SwInstallerPlanAction action;
            action.kind = SwInstallerActionKind::RemoveOwnedFile;
            action.targetPath = SwString(fileObj["path"].toString());
            action.checksumSha256 = SwString(fileObj["checksumSha256"].toString());
            action.preserveIfModified = fileObj["preserveIfModified"].toBool(false);
            fileActions.append(action);
        }
        appendSortedByDepth_(fileActions, plan.actions);
    }

    if (state.contains("ownedDirectories") && state["ownedDirectories"].isArray()) {
        const SwJsonArray arr = state["ownedDirectories"].toArray();
        SwList<SwInstallerPlanAction> dirActions;
        for (size_t i = 0; i < arr.size(); ++i) {
            SwInstallerPlanAction action;
            action.kind = SwInstallerActionKind::RemoveOwnedDirectory;
            action.targetPath = SwString(arr[i].toString());
            dirActions.append(action);
        }
        appendSortedByDepth_(dirActions, plan.actions);
    }

    SwInstallerPlanAction removeState;
    removeState.kind = SwInstallerActionKind::RemoveStateFile;
    removeState.targetPath = plan.stateFilePath;
    plan.actions.append(removeState);
}

inline SwJsonObject SwInstallerEngine::initialStateObject_(const SwInstallerPlan& plan) const {
    SwJsonObject state;
    state["productId"] = product_.productId.toStdString();
    state["displayName"] = product_.effectiveDisplayName().toStdString();
    state["publisher"] = product_.publisher.toStdString();
    state["version"] = product_.version.toStdString();
    state["installRoot"] = plan.installRoot.toStdString();
    state["stateRoot"] = plan.stateRoot.toStdString();
    state["stateFilePath"] = plan.stateFilePath.toStdString();
    state["cachedSetupPath"] = plan.cachedSetupPath.toStdString();
    state["mainExecutable"] = product_.mainExecutableRelativePath.toStdString();
    state["ownedFiles"] = SwJsonArray();
    state["ownedDirectories"] = SwJsonArray();
    state["shortcuts"] = SwJsonArray();
    state["registryKeys"] = SwJsonArray();
    SwJsonArray selected;
    for (size_t i = 0; i < plan.selectedComponentIds.size(); ++i) {
        selected.append(plan.selectedComponentIds[i].toStdString());
    }
    state["selectedComponents"] = selected;
    return state;
}

inline bool SwInstallerEngine::saveStateObject_(const SwString& path,
                                                const SwJsonObject& state,
                                                SwString* errOut) {
    const SwString directory = parentPath_(path);
    if (!directory.isEmpty() && !SwDir::mkpathAbsolute(directory, false)) {
        if (errOut) *errOut = SwString("failed to create state directory: ") + directory;
        return false;
    }
    SwFile file(path);
    if (!file.open(SwFile::Write)) {
        if (errOut) *errOut = SwString("failed to open installer state file: ") + path;
        return false;
    }
    const SwString content = SwJsonDocument(state).toJson(SwJsonDocument::JsonFormat::Pretty);
    const bool ok = file.write(content);
    file.close();
    if (!ok && errOut) {
        *errOut = SwString("failed to write installer state file: ") + path;
    }
    return ok;
}

inline SwJsonObject SwInstallerEngine::loadStateObject_(const SwString& path, SwString* errOut) {
    SwJsonObject state;
    const SwFileInfo info(path.toStdString());
    if (!info.exists() || info.isDir()) {
        return state;
    }
    SwFile file(path);
    if (!file.open(SwFile::Read)) {
        if (errOut) *errOut = SwString("failed to open installer state file: ") + path;
        return state;
    }
    const SwString text = file.readAll();
    file.close();
    SwString parseErr;
    const SwJsonDocument doc = SwJsonDocument::fromJson(text.toStdString(), parseErr);
    if (!parseErr.isEmpty() || !doc.isObject()) {
        if (errOut) {
            *errOut = parseErr.isEmpty() ? SwString("installer state root is not an object") : parseErr;
        }
        return SwJsonObject();
    }
    if (errOut) {
        errOut->clear();
    }
    return doc.object();
}

inline void SwInstallerEngine::appendOwnedFile_(SwJsonObject& state,
                                                const SwString& path,
                                                const SwString& checksum,
                                                bool preserveIfModified) {
    if (!state.contains("ownedFiles") || !state["ownedFiles"].isArray()) {
        state["ownedFiles"] = SwJsonArray();
    }
    SwJsonArray arr = state["ownedFiles"].toArray();
    SwJsonObject item;
    item["path"] = path.toStdString();
    item["checksumSha256"] = checksum.toStdString();
    item["preserveIfModified"] = preserveIfModified;
    arr.append(item);
    state["ownedFiles"] = arr;
}

inline void SwInstallerEngine::collectOwnedDirectoriesFromPath_(SwJsonObject& state,
                                                                SwString path,
                                                                const SwString& stopRoot) {
    while (!path.isEmpty()) {
        appendUniqueStringArray_(state, "ownedDirectories", path);
        if (path == stopRoot) {
            break;
        }
        const SwString parent = parentPath_(path);
        if (parent.isEmpty() || parent == path) {
            break;
        }
        path = parent;
    }
}

inline void SwInstallerEngine::appendUniqueStringArray_(SwJsonObject& state,
                                                        const SwString& key,
                                                        const SwString& value) {
    SwJsonArray arr;
    if (state.contains(key) && state[key].isArray()) {
        arr = state[key].toArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            if (SwString(arr[i].toString()) == value) {
                state[key] = arr;
                return;
            }
        }
    }
    arr.append(value.toStdString());
    state[key] = arr;
}

inline SwInstallerExecutionResult SwInstallerEngine::execute(const SwInstallerPlan& plan,
                                                             bool allowPostInstallLaunch,
                                                             SwString* errOut) {
    clearRollback_();

    SwInstallerExecutionResult result;
    if ((plan.kind == SwInstallerPlanKind::Install || plan.kind == SwInstallerPlanKind::Repair) &&
        (!SwDir::mkpathAbsolute(plan.installRoot, false) || !SwDir::mkpathAbsolute(plan.stateRoot, false))) {
        result.message = "failed to create install/state directories";
        if (errOut) *errOut = result.message;
        return result;
    }

    SwJsonObject state = (plan.kind == SwInstallerPlanKind::Uninstall)
                             ? loadStateObject_(plan.stateFilePath, nullptr)
                             : initialStateObject_(plan);

    for (size_t i = 0; i < plan.actions.size(); ++i) {
        SwString actionErr;
        if (!executeAction_(plan, plan.actions[i], state, &actionErr)) {
            if (plan.actions[i].optional) {
                continue;
            }
            result.message = actionErr.isEmpty()
                                 ? (SwString("installer action failed: ") + plan.actions[i].title)
                                 : actionErr;
            if (plan.kind != SwInstallerPlanKind::Uninstall) {
                SwString rollbackErr;
                (void)rollback(&rollbackErr);
            }
            if (errOut) *errOut = result.message;
            return result;
        }
    }

    if (plan.kind == SwInstallerPlanKind::Install || plan.kind == SwInstallerPlanKind::Repair) {
        if (!saveStateObject_(plan.stateFilePath, state, errOut)) {
            result.message = errOut ? *errOut : SwString("failed to save state file");
            SwString rollbackErr;
            (void)rollback(&rollbackErr);
            return result;
        }
        rollbackState_.stateFilePath = plan.stateFilePath;
    } else {
        clearRollback_();
    }

    result.ok = true;
    result.message = SwString("installer ") + SwInstallerPlan::kindToString(plan.kind) + " completed";
    result.stateObject = state;

    if (allowPostInstallLaunch &&
        (plan.kind == SwInstallerPlanKind::Install || plan.kind == SwInstallerPlanKind::Repair)) {
        SwString launchErr;
        (void)launchPostInstallActions(plan, &launchErr);
    }
    return result;
}

inline bool SwInstallerEngine::rollback(SwString* errOut) {
    for (size_t i = 0; i < rollbackState_.shortcuts.size(); ++i) {
        (void)SwInstallerWindows::deleteFileOrScheduleReboot(rollbackState_.shortcuts[i]);
    }
    for (size_t i = 0; i < rollbackState_.registryKeys.size(); ++i) {
        (void)SwInstallerWindows::removeUninstallEntry(rollbackState_.registryKeys[i], nullptr);
    }
    for (size_t i = 0; i < rollbackState_.files.size(); ++i) {
        (void)SwInstallerWindows::deleteFileOrScheduleReboot(rollbackState_.files[i]);
    }
    removeDirectoriesReverse_(rollbackState_.directories);
    if (!rollbackState_.stateFilePath.isEmpty()) {
        (void)SwInstallerWindows::deleteFileOrScheduleReboot(rollbackState_.stateFilePath);
    }
    clearRollback_();
    if (errOut) errOut->clear();
    return true;
}

inline bool SwInstallerEngine::launchPostInstallActions(const SwInstallerPlan& plan, SwString* errOut) const {
    for (size_t i = 0; i < plan.finalLaunches.size(); ++i) {
        const SwInstallerLaunchSpec& launch = plan.finalLaunches[i];
        const SwString exePath = SwInstallerPayload::joinRootAndRelative(plan.installRoot,
                                                                         launch.targetRelativePath);
        const SwString args = expandTokens_(launch.arguments, plan.installRoot, plan.stateRoot);
        const SwString workingDir =
            launch.workingDirectoryRelativePath.isEmpty()
                ? parentPath_(exePath)
                : SwInstallerPayload::joinRootAndRelative(
                      plan.installRoot,
                      expandTokens_(launch.workingDirectoryRelativePath, plan.installRoot, plan.stateRoot));
        if (!launchDetached_(exePath, splitCommandLine_(args), workingDir, errOut)) {
            return false;
        }
    }
    return true;
}

inline bool SwInstallerEngine::executeAction_(const SwInstallerPlan& plan,
                                              const SwInstallerPlanAction& action,
                                              SwJsonObject& state,
                                              SwString* errOut) {
    switch (action.kind) {
    case SwInstallerActionKind::ExtractPayloadFile:
        return executeExtractPayload_(action, plan, state, errOut);
    case SwInstallerActionKind::WriteJsonFile:
        return executeWriteJson_(action, plan, state, errOut);
    case SwInstallerActionKind::RunPrerequisite:
        return executePrerequisite_(action, errOut);
    case SwInstallerActionKind::CopySetupSelf:
        return executeCopySetup_(action, plan, state, errOut);
    case SwInstallerActionKind::CreateShortcut:
        return executeCreateShortcut_(action, state, errOut);
    case SwInstallerActionKind::RegisterUninstall:
        return executeRegisterUninstall_(action, plan, state, errOut);
    case SwInstallerActionKind::RemoveOwnedFile:
        return executeRemoveOwnedFile_(action, errOut);
    case SwInstallerActionKind::RemoveOwnedDirectory:
        return executeRemoveOwnedDirectory_(action, errOut);
    case SwInstallerActionKind::RemoveShortcut:
        return executeRemoveShortcut_(action, errOut);
    case SwInstallerActionKind::RemoveUninstallRegistration:
        return executeRemoveUninstallRegistration_(action, errOut);
    case SwInstallerActionKind::RemoveStateFile:
        return executeRemoveStateFile_(action, errOut);
    default:
        if (errOut) *errOut = SwString("unsupported installer action: ") + action.title;
        return false;
    }
}

inline bool SwInstallerEngine::executeExtractPayload_(const SwInstallerPlanAction& action,
                                                      const SwInstallerPlan& plan,
                                                      SwJsonObject& state,
                                                      SwString* errOut) {
    const SwInstallerEmbeddedPayload* payload = product_.findPayload(action.payloadId);
    if (!payload) {
        if (errOut) *errOut = SwString("payload not registered: ") + action.payloadId;
        return false;
    }
    if (!SwInstallerPayload::extractFile(*payload, action.payloadRelativePath, plan.installRoot, nullptr, errOut)) {
        return false;
    }
    appendOwnedFile_(state, action.targetPath, action.checksumSha256, false);
    collectOwnedDirectoriesFromPath_(state, parentPath_(action.targetPath), plan.installRoot);
    appendRollbackFile_(action.targetPath);
    appendRollbackDirectoriesFromPath_(parentPath_(action.targetPath), plan.installRoot);
    return true;
}

inline bool SwInstallerEngine::executeWriteJson_(const SwInstallerPlanAction& action,
                                                 const SwInstallerPlan& plan,
                                                 SwJsonObject& state,
                                                 SwString* errOut) {
    const SwString directory = parentPath_(action.targetPath);
    if (!directory.isEmpty() && !SwDir::mkpathAbsolute(directory, false)) {
        if (errOut) *errOut = SwString("failed to create directory for json file: ") + directory;
        return false;
    }
    const SwString tempPath = action.targetPath + ".swjson.tmp";
    SwFile file(tempPath);
    if (!file.open(SwFile::Write)) {
        if (errOut) *errOut = SwString("failed to open json temp file: ") + tempPath;
        return false;
    }
    const SwString jsonText = SwJsonDocument(action.jsonDocument).toJson(SwJsonDocument::JsonFormat::Pretty);
    if (!file.write(jsonText)) {
        file.close();
        if (errOut) *errOut = SwString("failed to write json temp file: ") + tempPath;
        return false;
    }
    file.close();
    if (!replaceFile_(tempPath, action.targetPath, errOut)) {
        return false;
    }
    appendOwnedFile_(state, action.targetPath, checksumForFile_(action.targetPath), action.preserveIfModified);
    collectOwnedDirectoriesFromPath_(state, directory, plan.installRoot);
    appendRollbackFile_(action.targetPath);
    appendRollbackDirectoriesFromPath_(directory, plan.installRoot);
    return true;
}

inline bool SwInstallerEngine::executePrerequisite_(const SwInstallerPlanAction& action, SwString* errOut) {
    unsigned long exitCode = 1;
    if (!SwInstallerWindows::runProcessAndWait(action.targetPath,
                                               splitCommandLine_(action.arguments),
                                               action.workingDirectory,
                                               action.createNoWindow,
                                               exitCode,
                                               errOut)) {
        return false;
    }
    if (static_cast<long long>(exitCode) != action.expectedExitCode) {
        if (errOut) *errOut = SwString("prerequisite exited with unexpected code: ") + action.targetPath;
        return false;
    }
    return true;
}

inline bool SwInstallerEngine::executeCopySetup_(const SwInstallerPlanAction& action,
                                                 const SwInstallerPlan& plan,
                                                 SwJsonObject& state,
                                                 SwString* errOut) {
    const SwString directory = parentPath_(action.targetPath);
    if (!directory.isEmpty() && !SwDir::mkpathAbsolute(directory, false)) {
        if (errOut) *errOut = SwString("failed to create cached setup directory: ") + directory;
        return false;
    }
    if (!SwFile::copy(action.sourcePath, action.targetPath, true)) {
        if (errOut) *errOut = SwString("failed to copy setup executable to: ") + action.targetPath;
        return false;
    }
    appendOwnedFile_(state, action.targetPath, checksumForFile_(action.targetPath), false);
    collectOwnedDirectoriesFromPath_(state, directory, plan.stateRoot);
    appendRollbackFile_(action.targetPath);
    appendRollbackDirectoriesFromPath_(directory, plan.stateRoot);
    return true;
}

inline bool SwInstallerEngine::executeCreateShortcut_(const SwInstallerPlanAction& action,
                                                      SwJsonObject& state,
                                                      SwString* errOut) {
    if (!SwInstallerWindows::createShortcut(action.linkPath,
                                            action.sourcePath,
                                            action.arguments,
                                            action.workingDirectory,
                                            action.description,
                                            action.iconPath,
                                            action.iconIndex,
                                            errOut)) {
        return false;
    }
    appendUniqueStringArray_(state, "shortcuts", action.linkPath);
    if (!rollbackState_.shortcuts.contains(action.linkPath)) {
        rollbackState_.shortcuts.append(action.linkPath);
    }
    return true;
}

inline bool SwInstallerEngine::executeRegisterUninstall_(const SwInstallerPlanAction& action,
                                                         const SwInstallerPlan& plan,
                                                         SwJsonObject& state,
                                                         SwString* errOut) {
    if (!SwInstallerWindows::writeUninstallEntry(action.registryKeyPath,
                                                 product_.effectiveDisplayName(),
                                                 product_.publisher,
                                                 product_.version,
                                                 plan.installRoot,
                                                 action.arguments,
                                                 action.sourcePath,
                                                 errOut)) {
        return false;
    }
    appendUniqueStringArray_(state, "registryKeys", action.registryKeyPath);
    if (!rollbackState_.registryKeys.contains(action.registryKeyPath)) {
        rollbackState_.registryKeys.append(action.registryKeyPath);
    }
    return true;
}

inline bool SwInstallerEngine::executeRemoveOwnedFile_(const SwInstallerPlanAction& action, SwString* errOut) {
    if (action.targetPath.isEmpty()) {
        return true;
    }
    if (!SwFile::exists(action.targetPath) || SwDir::exists(action.targetPath)) {
        return true;
    }
    if (action.preserveIfModified && !action.checksumSha256.isEmpty()) {
        const SwString currentChecksum = checksumForFile_(action.targetPath);
        if (!currentChecksum.isEmpty() && currentChecksum != action.checksumSha256) {
            return true;
        }
    }
    if (!SwInstallerWindows::deleteFileOrScheduleReboot(action.targetPath)) {
        if (errOut) *errOut = SwString("failed to delete file during uninstall: ") + action.targetPath;
        return false;
    }
    return true;
}

inline bool SwInstallerEngine::executeRemoveOwnedDirectory_(const SwInstallerPlanAction& action, SwString* errOut) {
    SW_UNUSED(errOut)
    if (action.targetPath.isEmpty()) {
        return true;
    }
    (void)SwInstallerWindows::deleteDirectoryIfEmpty(action.targetPath);
    return true;
}

inline bool SwInstallerEngine::executeRemoveShortcut_(const SwInstallerPlanAction& action, SwString* errOut) {
    if (!SwInstallerWindows::deleteFileOrScheduleReboot(action.linkPath)) {
        if (errOut) *errOut = SwString("failed to delete shortcut: ") + action.linkPath;
        return false;
    }
    return true;
}

inline bool SwInstallerEngine::executeRemoveUninstallRegistration_(const SwInstallerPlanAction& action,
                                                                   SwString* errOut) {
    return SwInstallerWindows::removeUninstallEntry(action.registryKeyPath, errOut);
}

inline bool SwInstallerEngine::executeRemoveStateFile_(const SwInstallerPlanAction& action, SwString* errOut) {
    if (action.targetPath.isEmpty()) {
        return true;
    }
    if (!SwInstallerWindows::deleteFileOrScheduleReboot(action.targetPath)) {
        if (errOut) *errOut = SwString("failed to delete installer state file: ") + action.targetPath;
        return false;
    }
    (void)SwInstallerWindows::deleteDirectoryIfEmpty(parentPath_(action.targetPath));
    return true;
}

inline void SwInstallerEngine::appendRollbackFile_(const SwString& path) {
    if (!rollbackState_.files.contains(path)) {
        rollbackState_.files.append(path);
    }
}

inline void SwInstallerEngine::appendRollbackDirectoriesFromPath_(SwString path, const SwString& stopRoot) {
    while (!path.isEmpty()) {
        if (!rollbackState_.directories.contains(path)) {
            rollbackState_.directories.append(path);
        }
        if (path == stopRoot) {
            break;
        }
        const SwString parent = parentPath_(path);
        if (parent.isEmpty() || parent == path) {
            break;
        }
        path = parent;
    }
}

inline void SwInstallerEngine::clearRollback_() {
    rollbackState_ = RollbackState_();
}

inline void SwInstallerEngine::appendSortedByDepth_(const SwList<SwInstallerPlanAction>& input,
                                                    SwList<SwInstallerPlanAction>& output) {
    SwList<SwInstallerPlanAction> sorted;
    for (size_t i = 0; i < input.size(); ++i) {
        size_t insertIndex = 0;
        while (insertIndex < sorted.size() &&
               sorted[insertIndex].targetPath.size() >= input[i].targetPath.size()) {
            ++insertIndex;
        }
        sorted.insert(insertIndex, input[i]);
    }
    for (size_t i = 0; i < sorted.size(); ++i) {
        output.append(sorted[i]);
    }
}

inline void SwInstallerEngine::removeDirectoriesReverse_(const SwList<SwString>& dirs) {
    SwList<SwString> sorted;
    for (size_t i = 0; i < dirs.size(); ++i) {
        size_t insertIndex = 0;
        while (insertIndex < sorted.size() && sorted[insertIndex].size() >= dirs[i].size()) {
            ++insertIndex;
        }
        sorted.insert(insertIndex, dirs[i]);
    }
    for (size_t i = 0; i < sorted.size(); ++i) {
        (void)SwInstallerWindows::deleteDirectoryIfEmpty(sorted[i]);
    }
}

inline SwString SwInstallerEngine::parentPath_(SwString path) {
    path.replace("\\", "/");
    const size_t slash = path.lastIndexOf('/');
    if (slash == static_cast<size_t>(-1)) {
        return SwString();
    }
    return path.left(static_cast<int>(slash));
}

inline SwString SwInstallerEngine::expandTokens_(SwString value,
                                                 const SwString& installRoot,
                                                 const SwString& stateRoot) {
    value.replace("{installRoot}", installRoot);
    value.replace("{stateRoot}", stateRoot);
    return value;
}

inline SwString SwInstallerEngine::displayIconAbsolute_(const SwString& installRoot) const {
    const SwString relative = product_.displayIconRelativePath.isEmpty()
                                  ? product_.mainExecutableRelativePath
                                  : product_.displayIconRelativePath;
    return relative.isEmpty() ? SwString()
                              : SwInstallerPayload::joinRootAndRelative(installRoot, relative);
}

inline SwList<SwString> SwInstallerEngine::splitCommandLine_(const SwString& text) {
    SwList<SwString> args;
    SwString current;
    bool inQuotes = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (!inQuotes && (ch == ' ' || ch == '\t')) {
            if (!current.isEmpty()) {
                args.append(current);
                current.clear();
            }
            continue;
        }
        current += ch;
    }
    if (!current.isEmpty()) {
        args.append(current);
    }
    return args;
}

inline SwString SwInstallerEngine::checksumForFile_(const SwString& path) {
    try {
        return SwCrypto::calculateFileChecksum(path.toStdString());
    } catch (...) {
        return SwString();
    }
}

inline bool SwInstallerEngine::replaceFile_(const SwString& tempPath,
                                            const SwString& destinationPath,
                                            SwString* errOut) {
#if defined(_WIN32)
    if (::MoveFileExW(tempPath.toStdWString().c_str(),
                      destinationPath.toStdWString().c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        return true;
    }
    if (errOut) *errOut = SwString("failed to replace file: ") + destinationPath;
    return false;
#else
    if (::rename(tempPath.toStdString().c_str(), destinationPath.toStdString().c_str()) == 0) {
        return true;
    }
    if (errOut) *errOut = SwString("failed to replace file: ") + destinationPath;
    return false;
#endif
}

inline bool SwInstallerEngine::launchDetached_(const SwString& executablePath,
                                               const SwList<SwString>& arguments,
                                               const SwString& workingDirectory,
                                               SwString* errOut) {
#if defined(_WIN32)
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    const std::wstring exe = executablePath.toStdWString();
    std::wstring args;
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (!args.empty()) args.push_back(L' ');
        args += quoteArgument_(arguments[i].toStdWString());
    }
    const std::wstring cwd = workingDirectory.toStdWString();
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.lpDirectory = cwd.empty() ? nullptr : cwd.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!::ShellExecuteExW(&sei)) {
        if (errOut) *errOut = SwString("failed to launch final executable: ") + executablePath;
        return false;
    }
    return true;
#else
    (void)executablePath;
    (void)arguments;
    (void)workingDirectory;
    if (errOut) *errOut = "detached launch is only available on Windows";
    return false;
#endif
}

inline std::wstring SwInstallerEngine::quoteArgument_(const std::wstring& s) {
    if (s.empty()) {
        return L"\"\"";
    }
    if (s.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return s;
    }
    std::wstring out;
    out.push_back(L'"');
    size_t backslashes = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t ch = s[i];
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(ch);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

} // namespace swinstaller
