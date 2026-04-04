#pragma once

/**
 * @file src/core/installer/SwInstallerWizardApp.h
 * @brief Header-only SwWizard facade for the Sw installer runtime.
 */

#include "SwInstaller.h"
#include "SwInstallerShellDialog.h"

#include "SwCheckBox.h"
#include "SwDateTime.h"
#include "SwGuiApplication.h"
#include "SwLabel.h"
#include "SwLineEdit.h"
#include "SwMap.h"
#include "SwMessageBox.h"
#include "SwPathDialog.h"
#include "SwPlainTextEdit.h"
#include "SwProgressBar.h"
#include "SwPushButton.h"
#include "SwWidgetSnapshot.h"

namespace swinstaller {

class SwInstallerWizardApp {
public:
    explicit SwInstallerWizardApp(const SwInstallerProduct& product,
                                  const SwString& setupSelfPath = SwInstallerWindows::currentExecutablePath());

    static int run(SwGuiApplication& app,
                   int argc,
                   char* argv[],
                   const SwInstallerProduct& product,
                   SwWidget* parent = nullptr,
                   const SwString& setupSelfPath = SwInstallerWindows::currentExecutablePath());

    static bool saveSnapshots(SwGuiApplication& app,
                              const SwInstallerProduct& product,
                              const SwString& outputDirectory,
                              SwWidget* parent = nullptr,
                              const SwString& setupSelfPath = SwInstallerWindows::currentExecutablePath());

private:
    class WelcomePage_ : public SwWizardPage {
        SW_OBJECT(WelcomePage_, SwWizardPage)
    public:
        WelcomePage_(const SwString& productName, bool installed, SwWidget* parent = nullptr);

    protected:
        void resizeEvent(ResizeEvent* event) override;

    private:
        SwLabel* body_{nullptr};
        SwLabel* details_{nullptr};
    };

    class TargetPage_ : public SwWizardPage {
        SW_OBJECT(TargetPage_, SwWizardPage)
    public:
        explicit TargetPage_(SwWidget* parent = nullptr);
        SwString installRoot() const;
        void setInstallRoot(const SwString& value);

    protected:
        void resizeEvent(ResizeEvent* event) override;

    private:
        SwLabel* label_{nullptr};
        SwLineEdit* edit_{nullptr};
        SwPushButton* browse_{nullptr};
        SwLabel* hint_{nullptr};
    };

    class ComponentsPage_ : public SwWizardPage {
        SW_OBJECT(ComponentsPage_, SwWizardPage)
    public:
        explicit ComponentsPage_(const SwInstallerProduct& product, SwWidget* parent = nullptr);
        SwList<SwString> selectedComponents() const;
        void setSelectedComponents(const SwList<SwString>& ids);

    protected:
        void resizeEvent(ResizeEvent* event) override;

    private:
        SwLabel* intro_{nullptr};
        SwList<SwCheckBox*> checkBoxes_;
        SwList<SwString> componentIds_;
        SwList<SwLabel*> descriptions_;
    };

    class ProgressPage_ : public SwWizardPage {
        SW_OBJECT(ProgressPage_, SwWizardPage)
    public:
        explicit ProgressPage_(SwWidget* parent = nullptr);
        void reset();
        void setStatus(const SwString& value);
        void setProgress(int value);
        void appendLog(const SwString& value);

    protected:
        void resizeEvent(ResizeEvent* event) override;

    private:
        SwLabel* status_{nullptr};
        SwProgressBar* progress_{nullptr};
        SwPlainTextEdit* log_{nullptr};
    };

    class FinishPage_ : public SwWizardPage {
        SW_OBJECT(FinishPage_, SwWizardPage)
    public:
        explicit FinishPage_(SwWidget* parent = nullptr);
        void setSummary(const SwString& text);
        void setLaunchVisible(bool visible);
        void setLaunchChecked(bool checked);
        bool launchAfterFinish() const;

    protected:
        void resizeEvent(ResizeEvent* event) override;

    private:
        SwLabel* summary_{nullptr};
        SwCheckBox* launch_{nullptr};
    };

    int run_(int argc, char* argv[], SwWidget* parent);
    bool saveSnapshots_(SwGuiApplication& app, const SwString& outputDirectory, SwWidget* parent);
    bool handleExecutePlanMode_(int& exitCode);
    bool acquireSingleInstanceMutex_(SwWidget* parent);
    int runWizard_(SwWidget* parent);
    void onWizardPageChanged_(int currentId);
    void runWizardInstall_();

    int runRepairInteractive_(SwWidget* parent, bool allowPostInstallLaunch);
    int runRepairSilent_(bool allowPostInstallLaunch);
    int runUninstallInteractive_(SwWidget* parent, bool allowPostInstallLaunch);
    int runUninstallSilent_(bool allowPostInstallLaunch);

    bool runPlanWithElevation_(const SwInstallerPlan& plan,
                               bool allowPostInstallLaunch,
                               SwInstallerExecutionResult& result,
                               SwString* errOut);

    static bool writeResultFileIfNeeded_(const SwString& resultPath,
                                         const SwInstallerExecutionResult& result);
    static bool writeTextFile_(const SwString& path, const SwString& text, SwString* errOut);
    static SwString readTextFile_(const SwString& path, SwString* errOut);
    static SwString parentPath_(SwString path);

    SwString temporaryRoot_() const;
    SwString uniqueToken_() const;
    SwList<SwString> defaultSelectedComponents_() const;

    static SwList<SwString> parseCsv_(const SwString& text);
    static SwMap<SwString, SwString> parseArguments_(int argc, char* argv[]);
    static bool parseBool_(SwString value);
    static SwString normalizePath_(SwString path);

    static void styleTitleText_(SwLabel* label, int fontSize, bool accent);
    static void styleBodyText_(SwLabel* label);
    static void styleLineEdit_(SwLineEdit* edit);
    static void styleActionButton_(SwPushButton* button);
    static void styleCheckBox_(SwCheckBox* box);
    static void styleProgress_(SwProgressBar* bar);
    static void styleLog_(SwPlainTextEdit* edit);

    SwInstallerProduct product_;
    SwString setupSelfPath_;
    SwInstallerEngine engine_;

    SwInstallerPlan activePlan_;
    SwInstallerExecutionResult lastResult_;
    bool installInProgress_{false};
    bool progressTaskPosted_{false};
    SwMap<SwString, SwString> arguments_;

    SwInstallerShellDialog* wizard_{nullptr};
    WelcomePage_* welcomePage_{nullptr};
    TargetPage_* targetPage_{nullptr};
    ComponentsPage_* componentsPage_{nullptr};
    ProgressPage_* progressPage_{nullptr};
    FinishPage_* finishPage_{nullptr};

#if defined(_WIN32)
    SwInstallerWindows::MutexHandle mutexHandle_{};
#endif
};

} // namespace swinstaller

namespace swinstaller {

inline SwInstallerWizardApp::SwInstallerWizardApp(const SwInstallerProduct& product,
                                                  const SwString& setupSelfPath)
    : product_(product),
      setupSelfPath_(setupSelfPath.isEmpty() ? SwInstallerWindows::currentExecutablePath()
                                             : setupSelfPath),
      engine_(product_, setupSelfPath_) {}

inline int SwInstallerWizardApp::run(SwGuiApplication& app,
                                     int argc,
                                     char* argv[],
                                     const SwInstallerProduct& product,
                                     SwWidget* parent,
                                     const SwString& setupSelfPath) {
    SW_UNUSED(app)
    SwInstallerWizardApp wizardApp(product, setupSelfPath);
    return wizardApp.run_(argc, argv, parent);
}

inline bool SwInstallerWizardApp::saveSnapshots(SwGuiApplication& app,
                                                const SwInstallerProduct& product,
                                                const SwString& outputDirectory,
                                                SwWidget* parent,
                                                const SwString& setupSelfPath) {
    SwInstallerWizardApp wizardApp(product, setupSelfPath);
    return wizardApp.saveSnapshots_(app, outputDirectory, parent);
}

inline void SwInstallerWizardApp::styleTitleText_(SwLabel* label, int fontSize, bool accent) {
    if (!label) return;
    label->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top |
                                        DrawTextFormat::WordBreak));
    label->setStyleSheet(SwString("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: ") +
                         (accent ? SwString("rgb(240, 240, 240); ") : SwString("rgb(200, 200, 200); ")) +
                         SwString("font-size: ") + SwString::number(fontSize) + "px; " +
                         (accent ? SwString("font-weight: bold; }") : SwString("}")));
}

inline void SwInstallerWizardApp::styleBodyText_(SwLabel* label) {
    if (!label) return;
    label->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top |
                                        DrawTextFormat::WordBreak));
    label->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; "
                         "color: rgb(160, 160, 164); font-size: 13px; }");
}

inline void SwInstallerWizardApp::styleLineEdit_(SwLineEdit* edit) {
    if (!edit) return;
    edit->setStyleSheet("SwLineEdit { background-color: rgb(37, 37, 38); "
                        "border-color: rgb(70, 70, 74); border-width: 1px; "
                        "border-radius: 6px; padding: 6px 10px; "
                        "color: rgb(220, 220, 220); font-size: 13px; }");
}

inline void SwInstallerWizardApp::styleActionButton_(SwPushButton* button) {
    if (!button) return;
    button->setStyleSheet("SwPushButton { background-color: rgba(0,0,0,0); "
                          "border-color: rgb(70, 70, 74); border-width: 1px; "
                          "border-radius: 6px; color: rgb(200, 200, 200); "
                          "padding: 6px 14px; font-size: 13px; } "
                          "SwPushButton:hover { background-color: rgb(50, 50, 54); } "
                          "SwPushButton:pressed { background-color: rgb(60, 60, 64); }");
}

inline void SwInstallerWizardApp::styleCheckBox_(SwCheckBox* box) {
    if (!box) return;
    box->setStyleSheet("SwCheckBox { background-color: rgba(0,0,0,0); border-width: 0px; "
                       "color: rgb(220, 220, 220); font-size: 13px; "
                       "background-color-unchecked: rgb(37, 37, 38); "
                       "border-color-unchecked: rgb(70, 70, 74); }");
}

inline void SwInstallerWizardApp::styleProgress_(SwProgressBar* bar) {
    if (!bar) return;
    bar->setStyleSheet("SwProgressBar { background-color: rgb(37, 37, 38); "
                       "border-color: rgb(50, 50, 54); border-width: 1px; "
                       "border-radius: 4px; color: rgb(200, 200, 200); }");
}

inline void SwInstallerWizardApp::styleLog_(SwPlainTextEdit* edit) {
    if (!edit) return;
    edit->setStyleSheet("SwPlainTextEdit { background-color: rgb(24, 24, 26); "
                        "border-color: rgb(50, 50, 54); border-width: 1px; "
                        "border-radius: 6px; color: rgb(190, 190, 190); "
                        "font-family: Consolas; font-size: 12px; }");
}

inline SwString SwInstallerWizardApp::normalizePath_(SwString path) {
    return SwPathDialog::normalizePath(path.trimmed());
}

inline SwInstallerWizardApp::WelcomePage_::WelcomePage_(const SwString& productName,
                                                        bool installed,
                                                        SwWidget* parent)
    : SwWizardPage(parent) {
    setTitle(installed ? SwString("Maintenance") : SwString("Welcome"));
    setSubTitle(installed ? SwString("Manage your installation")
                          : SwString("Let's get you set up"));

    body_ = new SwLabel(installed
                            ? SwString("You can repair or update your ") + productName +
                                  SwString(" installation.")
                            : SwString("This wizard will guide you through the installation of ") +
                                  productName + SwString("."),
                        this);
    details_ = new SwLabel(
        installed
            ? "Use the steps on the left to review and apply changes."
            : "Click Next to choose where to install and which components to include.",
        this);

    SwInstallerWizardApp::styleTitleText_(body_, 14, true);
    SwInstallerWizardApp::styleBodyText_(details_);
}

inline void SwInstallerWizardApp::WelcomePage_::resizeEvent(ResizeEvent* event) {
    SwWizardPage::resizeEvent(event);
    const int w = width();
    body_->move(0, 12);
    body_->resize(w, 48);
    details_->move(0, 72);
    details_->resize(w, 80);
}

inline SwInstallerWizardApp::TargetPage_::TargetPage_(SwWidget* parent)
    : SwWizardPage(parent) {
    setTitle("Destination");
    setSubTitle("Choose where to install");

    label_ = new SwLabel("Installation folder", this);
    edit_ = new SwLineEdit(this);
    browse_ = new SwPushButton("Browse...", this);
    hint_ = new SwLabel(
        "Files will be installed in a subfolder at this location. "
        "The installer state is stored separately under AppData.",
        this);

    SwInstallerWizardApp::styleTitleText_(label_, 13, false);
    SwInstallerWizardApp::styleLineEdit_(edit_);
    SwInstallerWizardApp::styleActionButton_(browse_);
    SwInstallerWizardApp::styleBodyText_(hint_);

    SwObject::connect(browse_, &SwPushButton::clicked, this, [this]() {
        const SwString selected = SwPathDialog::getExistingDirectory(
            this,
            "Select installation folder",
            installRoot());
        if (!selected.isEmpty()) {
            setInstallRoot(selected);
        }
    });
    SwObject::connect(edit_, &SwWidget::FocusChanged, this, [this](bool focus) {
        if (!focus) {
            setInstallRoot(installRoot());
        }
    });
}

inline SwString SwInstallerWizardApp::TargetPage_::installRoot() const {
    return edit_ ? SwInstallerWizardApp::normalizePath_(edit_->getText()) : SwString();
}

inline void SwInstallerWizardApp::TargetPage_::setInstallRoot(const SwString& value) {
    if (edit_) {
        edit_->setText(SwInstallerWizardApp::normalizePath_(value));
    }
}

inline void SwInstallerWizardApp::TargetPage_::resizeEvent(ResizeEvent* event) {
    SwWizardPage::resizeEvent(event);
    const int w = width();
    const int browseWidth = 100;
    const int gap = 8;
    int editWidth = w - browseWidth - gap;
    if (editWidth < 0) {
        editWidth = 0;
    }
    label_->move(0, 12);
    label_->resize(w, 22);
    edit_->move(0, 40);
    edit_->resize(editWidth, 34);
    browse_->move(editWidth + gap, 40);
    browse_->resize(browseWidth, 34);
    hint_->move(0, 90);
    hint_->resize(w, 70);
}

inline SwInstallerWizardApp::ComponentsPage_::ComponentsPage_(const SwInstallerProduct& product,
                                                              SwWidget* parent)
    : SwWizardPage(parent) {
    setTitle("Components");
    setSubTitle("Select the features to install");

    intro_ = new SwLabel(
        "Check the components you want to include. Each component may add files, "
        "configuration, shortcuts or services.",
        this);
    SwInstallerWizardApp::styleBodyText_(intro_);

    for (size_t i = 0; i < product.components.size(); ++i) {
        const SwInstallerComponent& component = product.components[i];
        SwCheckBox* check = new SwCheckBox(component.displayName.isEmpty() ? component.componentId
                                                                           : component.displayName,
                                           this);
        check->setChecked(component.selectedByDefault);
        SwInstallerWizardApp::styleCheckBox_(check);
        checkBoxes_.append(check);
        componentIds_.append(component.componentId);

        SwLabel* desc = new SwLabel(component.description.isEmpty()
                                        ? SwString("No additional description.")
                                        : component.description,
                                    this);
        SwInstallerWizardApp::styleBodyText_(desc);
        descriptions_.append(desc);
    }
}

inline SwList<SwString> SwInstallerWizardApp::ComponentsPage_::selectedComponents() const {
    SwList<SwString> out;
    for (size_t i = 0; i < checkBoxes_.size() && i < componentIds_.size(); ++i) {
        if (checkBoxes_[i] && checkBoxes_[i]->isChecked()) {
            out.append(componentIds_[i]);
        }
    }
    return out;
}

inline void SwInstallerWizardApp::ComponentsPage_::setSelectedComponents(const SwList<SwString>& ids) {
    for (size_t i = 0; i < checkBoxes_.size() && i < componentIds_.size(); ++i) {
        if (checkBoxes_[i]) {
            checkBoxes_[i]->setChecked(ids.contains(componentIds_[i]));
        }
    }
}

inline void SwInstallerWizardApp::ComponentsPage_::resizeEvent(ResizeEvent* event) {
    SwWizardPage::resizeEvent(event);
    const int w = width();
    int y = 12;
    intro_->move(0, y);
    intro_->resize(w, 48);
    y += 60;

    for (size_t i = 0; i < checkBoxes_.size(); ++i) {
        if (checkBoxes_[i]) {
            checkBoxes_[i]->move(4, y);
            checkBoxes_[i]->resize(w - 4, 24);
        }
        y += 28;
        if (i < descriptions_.size() && descriptions_[i]) {
            descriptions_[i]->move(28, y);
            descriptions_[i]->resize(w - 28, 36);
        }
        y += 44;
    }
}

inline SwInstallerWizardApp::ProgressPage_::ProgressPage_(SwWidget* parent)
    : SwWizardPage(parent) {
    setTitle("Installing");
    setSubTitle("Please wait while the installation completes");

    status_ = new SwLabel("Waiting to start...", this);
    progress_ = new SwProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setFormat("%p%");
    log_ = new SwPlainTextEdit(this);
    log_->setReadOnly(true);

    SwInstallerWizardApp::styleTitleText_(status_, 13, false);
    SwInstallerWizardApp::styleProgress_(progress_);
    SwInstallerWizardApp::styleLog_(log_);
}

inline void SwInstallerWizardApp::ProgressPage_::reset() {
    if (status_) status_->setText("Waiting to start...");
    if (progress_) progress_->setValue(0);
    if (log_) log_->clear();
}

inline void SwInstallerWizardApp::ProgressPage_::setStatus(const SwString& value) {
    if (status_) {
        status_->setText(value);
        status_->update();
    }
}

inline void SwInstallerWizardApp::ProgressPage_::setProgress(int value) {
    if (progress_) {
        progress_->setValue(value);
        progress_->update();
    }
}

inline void SwInstallerWizardApp::ProgressPage_::appendLog(const SwString& value) {
    if (log_) {
        log_->appendPlainText(value);
        log_->update();
    }
}

inline void SwInstallerWizardApp::ProgressPage_::resizeEvent(ResizeEvent* event) {
    SwWizardPage::resizeEvent(event);
    const int w = width();
    const int h = height();
    int logHeight = h - 80;
    if (logHeight < 100) {
        logHeight = 100;
    }
    status_->move(0, 12);
    status_->resize(w, 22);
    progress_->move(0, 42);
    progress_->resize(w, 14);
    log_->move(0, 72);
    log_->resize(w, logHeight);
}

inline SwInstallerWizardApp::FinishPage_::FinishPage_(SwWidget* parent)
    : SwWizardPage(parent) {
    setTitle("Complete");
    setSubTitle("Setup has finished");

    summary_ = new SwLabel("", this);
    launch_ = new SwCheckBox("Launch the application now", this);

    SwInstallerWizardApp::styleTitleText_(summary_, 13, false);
    SwInstallerWizardApp::styleCheckBox_(launch_);
    launch_->setChecked(true);
    launch_->setVisible(false);
}

inline void SwInstallerWizardApp::FinishPage_::setSummary(const SwString& text) {
    if (summary_) {
        summary_->setText(text);
        summary_->update();
    }
}

inline void SwInstallerWizardApp::FinishPage_::setLaunchVisible(bool visible) {
    if (launch_) {
        launch_->setVisible(visible);
        launch_->update();
    }
}

inline void SwInstallerWizardApp::FinishPage_::setLaunchChecked(bool checked) {
    if (launch_) {
        launch_->setChecked(checked);
    }
}

inline bool SwInstallerWizardApp::FinishPage_::launchAfterFinish() const {
    return launch_ && launch_->getVisible() && launch_->isChecked();
}

inline void SwInstallerWizardApp::FinishPage_::resizeEvent(ResizeEvent* event) {
    SwWizardPage::resizeEvent(event);
    const int w = width();
    summary_->move(0, 16);
    summary_->resize(w, 80);
    launch_->move(4, 110);
    launch_->resize(w - 4, 26);
}

inline SwList<SwString> SwInstallerWizardApp::parseCsv_(const SwString& text) {
    SwList<SwString> out;
    const SwList<SwString> parts = text.split(',');
    for (size_t i = 0; i < parts.size(); ++i) {
        const SwString value = parts[i].trimmed();
        if (!value.isEmpty()) {
            out.append(value);
        }
    }
    return out;
}

inline SwMap<SwString, SwString> SwInstallerWizardApp::parseArguments_(int argc, char* argv[]) {
    SwMap<SwString, SwString> out;
    for (int i = 1; i < argc; ++i) {
        SwString arg = argv[i];
        if (!arg.startsWith("-")) {
            continue;
        }

        bool longForm = false;
        if (arg.startsWith("--")) {
            arg.remove(0, 2);
            longForm = true;
        } else {
            arg.remove(0, 1);
        }

        const int eq = arg.indexOf('=');
        if (eq >= 0) {
            out[arg.left(eq)] = arg.mid(eq + 1);
            continue;
        }

        SwString value;
        if (i + 1 < argc) {
            const SwString next = argv[i + 1];
            if (!next.startsWith("-")) {
                value = next;
                ++i;
            }
        }
        out[arg] = value;
        if (!longForm && !value.isEmpty()) {
            out[arg] = value;
        }
    }
    return out;
}

inline bool SwInstallerWizardApp::parseBool_(SwString value) {
    value = value.trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

inline SwString SwInstallerWizardApp::parentPath_(SwString path) {
    path.replace("\\", "/");
    const size_t slash = path.lastIndexOf('/');
    if (slash == static_cast<size_t>(-1)) {
        return SwString();
    }
    return path.left(static_cast<int>(slash));
}

inline SwString SwInstallerWizardApp::temporaryRoot_() const {
    SwString base = SwStandardPaths::writableLocation(SwStandardPaths::TempLocation);
    if (base.isEmpty()) {
        base = product_.resolveStateRoot();
    }
    return normalizePath_(base + "/" + product_.productId + "-installer");
}

inline SwString SwInstallerWizardApp::uniqueToken_() const {
    const SwDateTime now;
    const long long timestamp = static_cast<long long>(now.toTimeT());
    long long monotonic = timestamp;
    long long pid = 0;
#if defined(_WIN32)
    monotonic = static_cast<long long>(::GetTickCount64());
    pid = static_cast<long long>(::GetCurrentProcessId());
#endif
    return product_.productId + "-" + SwString::number(timestamp) +
           "-" + SwString::number(monotonic) + "-" +
           SwString::number(pid);
}

inline SwList<SwString> SwInstallerWizardApp::defaultSelectedComponents_() const {
    SwList<SwString> out;
    for (size_t i = 0; i < product_.components.size(); ++i) {
        if (product_.components[i].selectedByDefault) {
            out.append(product_.components[i].componentId);
        }
    }
    if (out.isEmpty()) {
        for (size_t i = 0; i < product_.components.size(); ++i) {
            out.append(product_.components[i].componentId);
        }
    }
    return out;
}

inline bool SwInstallerWizardApp::writeTextFile_(const SwString& path,
                                                 const SwString& text,
                                                 SwString* errOut) {
    const SwString directory = parentPath_(path);
    if (!directory.isEmpty() && !SwDir::mkpathAbsolute(directory, false)) {
        if (errOut) *errOut = SwString("failed to create directory: ") + directory;
        return false;
    }
    SwFile file(path);
    if (!file.open(SwFile::Write)) {
        if (errOut) *errOut = SwString("failed to open file for write: ") + path;
        return false;
    }
    const bool ok = file.write(text);
    file.close();
    if (!ok && errOut) {
        *errOut = SwString("failed to write file: ") + path;
    }
    return ok;
}

inline SwString SwInstallerWizardApp::readTextFile_(const SwString& path, SwString* errOut) {
    SwFile file(path);
    if (!file.open(SwFile::Read)) {
        if (errOut) *errOut = SwString("failed to open file for read: ") + path;
        return SwString();
    }
    const SwString text = file.readAll();
    file.close();
    return text;
}

inline bool SwInstallerWizardApp::writeResultFileIfNeeded_(const SwString& resultPath,
                                                           const SwInstallerExecutionResult& result) {
    if (resultPath.isEmpty()) {
        return true;
    }
    SwString err;
    return writeTextFile_(resultPath, result.toJson(), &err);
}

inline int SwInstallerWizardApp::run_(int argc, char* argv[], SwWidget* parent) {
    arguments_ = parseArguments_(argc, argv);

    int executePlanExitCode = 0;
    if (handleExecutePlanMode_(executePlanExitCode)) {
        return executePlanExitCode;
    }

    if (!acquireSingleInstanceMutex_(parent)) {
        return 1;
    }

    const SwString mode = arguments_.contains("mode") ? arguments_["mode"].trimmed().toLower() : SwString();
    const bool silent = arguments_.contains("silent");
    const bool allowPostInstallLaunch =
        parseBool_(arguments_.contains("launch-post-install") ? arguments_["launch-post-install"] : SwString());

    if (mode == "uninstall") {
        return silent ? runUninstallSilent_(allowPostInstallLaunch)
                      : runUninstallInteractive_(parent, allowPostInstallLaunch);
    }

    if (mode == "repair") {
        return silent ? runRepairSilent_(allowPostInstallLaunch)
                      : runRepairInteractive_(parent, allowPostInstallLaunch);
    }

    if (silent) {
        const SwString installRootOverride =
            arguments_.contains("install-root") ? normalizePath_(arguments_["install-root"]) : SwString();
        const SwList<SwString> componentIds =
            parseCsv_(arguments_.contains("components") ? arguments_["components"] : SwString());
        SwInstallerPlan plan = engine_.planInstall(installRootOverride, componentIds, true);
        SwInstallerExecutionResult result;
        SwString err;
        if (!runPlanWithElevation_(plan, allowPostInstallLaunch, result, &err)) {
            return 1;
        }
        return result.ok ? 0 : 1;
    }

    return runWizard_(parent);
}

inline bool SwInstallerWizardApp::saveSnapshots_(SwGuiApplication& app,
                                                 const SwString& outputDirectory,
                                                 SwWidget* parent) {
    SW_UNUSED(app)
    SwString outDir = normalizePath_(outputDirectory);
    if (outDir.isEmpty()) {
        outDir = normalizePath_(temporaryRoot_() + "/snapshots");
    }
    if (!outDir.endsWith("/")) {
        outDir += "/";
    }
    if (!SwDir::mkpathAbsolute(outDir, false)) {
        return false;
    }

    SwInstallerShellDialog wizard(product_.effectiveDisplayName(), false, parent);
    wizard_ = &wizard;

    welcomePage_ = new WelcomePage_(product_.effectiveDisplayName(), false);
    targetPage_ = new TargetPage_();
    componentsPage_ = new ComponentsPage_(product_);
    progressPage_ = new ProgressPage_();
    finishPage_ = new FinishPage_();

    const SwString defaultInstallRoot = normalizePath_(product_.resolveDefaultInstallRoot());
    targetPage_->setInstallRoot(defaultInstallRoot);
    componentsPage_->setSelectedComponents(defaultSelectedComponents_());

    progressPage_->reset();
    progressPage_->setStatus("Installing components...");
    progressPage_->setProgress(64);
    progressPage_->appendLog("[info] Extracting payload files...");
    progressPage_->appendLog(SwString("[info] Target: ") + defaultInstallRoot);
    progressPage_->appendLog("[info] Writing configuration...");

    finishPage_->setSummary(SwString("Installation complete.\n\nInstalled to:\n") + defaultInstallRoot);
    finishPage_->setLaunchVisible(true);
    finishPage_->setLaunchChecked(true);

    wizard.addPage(welcomePage_);
    wizard.addPage(targetPage_);
    wizard.addPage(componentsPage_);
    wizard.addPage(progressPage_);
    wizard.addPage(finishPage_);

    bool ok = true;

    struct SnapshotStep_ {
        int pageId;
        const char* fileName;
    };

    const SnapshotStep_ steps[] = {
        {0, "installer_welcome.png"},
        {1, "installer_target.png"},
        {2, "installer_components.png"},
        {3, "installer_progress.png"},
        {4, "installer_finish.png"}
    };

    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
        wizard.setCurrentId(steps[i].pageId);
        ok = ok && SwWidgetSnapshot::savePng(&wizard, outDir + steps[i].fileName);
    }

    wizard_ = nullptr;
    welcomePage_ = nullptr;
    targetPage_ = nullptr;
    componentsPage_ = nullptr;
    progressPage_ = nullptr;
    finishPage_ = nullptr;
    return ok;
}

inline bool SwInstallerWizardApp::handleExecutePlanMode_(int& exitCode) {
    if (!arguments_.contains("execute-plan")) {
        return false;
    }

    SwInstallerExecutionResult result;
    SwString err;
    const SwString planPath = normalizePath_(arguments_["execute-plan"]);
    const SwString resultPath =
        arguments_.contains("result-file") ? normalizePath_(arguments_["result-file"]) : SwString();
    const bool allowPostInstallLaunch =
        parseBool_(arguments_.contains("launch-post-install") ? arguments_["launch-post-install"] : SwString());

    const SwString planJson = readTextFile_(planPath, &err);
    if (planJson.isEmpty() && !SwFile::exists(planPath)) {
        result.message = err.isEmpty() ? SwString("plan file not found: ") + planPath : err;
    } else {
        SwString parseErr;
        const SwInstallerPlan plan = SwInstallerPlan::fromJson(planJson, &parseErr);
        if (!parseErr.isEmpty()) {
            result.message = parseErr;
        } else {
            SwString execErr;
            result = engine_.execute(plan, allowPostInstallLaunch, &execErr);
            if (!result.ok && result.message.isEmpty()) {
                result.message = execErr.isEmpty() ? SwString("installer execution failed") : execErr;
            }
        }
    }

    if (!writeResultFileIfNeeded_(resultPath, result)) {
        exitCode = 1;
        return true;
    }

    exitCode = result.ok ? 0 : 1;
    return true;
}

inline bool SwInstallerWizardApp::acquireSingleInstanceMutex_(SwWidget* parent) {
#if defined(_WIN32)
    SwString err;
    if (!SwInstallerWindows::createSingleInstanceMutex(product_.productId, mutexHandle_, &err)) {
        SwMessageBox::critical(parent,
                               product_.effectiveDisplayName(),
                               err.isEmpty() ? SwString("Failed to create installer mutex") : err);
        return false;
    }
    if (mutexHandle_.alreadyExists) {
        SwMessageBox::warning(parent,
                              product_.effectiveDisplayName(),
                              "Another installer instance is already running for this product.");
        return false;
    }
#else
    SW_UNUSED(parent)
#endif
    return true;
}

inline int SwInstallerWizardApp::runWizard_(SwWidget* parent) {
    const SwInstallerDetection detection = engine_.detect();
    SwInstallerShellDialog wizard(product_.effectiveDisplayName(), detection.installed, parent);
    wizard_ = &wizard;

    welcomePage_ = new WelcomePage_(product_.effectiveDisplayName(), detection.installed);
    targetPage_ = new TargetPage_();
    componentsPage_ = new ComponentsPage_(product_);
    progressPage_ = new ProgressPage_();
    finishPage_ = new FinishPage_();

    const SwString defaultInstallRoot = detection.installRoot.isEmpty()
                                            ? normalizePath_(product_.resolveDefaultInstallRoot())
                                            : normalizePath_(detection.installRoot);
    targetPage_->setInstallRoot(defaultInstallRoot);
    componentsPage_->setSelectedComponents(defaultSelectedComponents_());

    wizard.addPage(welcomePage_);
    wizard.addPage(targetPage_);
    wizard.addPage(componentsPage_);
    wizard.addPage(progressPage_);
    wizard.addPage(finishPage_);

    finishPage_->setSummary(detection.installed
                                ? SwString("The product is already installed. The wizard will build a "
                                           "repair plan for the selected components.")
                                : SwString("The wizard will show the execution result here."));

    wizard.setCurrentIdChangedHandler([this](int currentId) {
        onWizardPageChanged_(currentId);
    });

    const int res = wizard.exec();
    if (res == SwDialog::Accepted && lastResult_.ok && finishPage_ && finishPage_->launchAfterFinish()) {
        SwString launchErr;
        if (!engine_.launchPostInstallActions(activePlan_, &launchErr)) {
            SwMessageBox::warning(parent,
                                  product_.effectiveDisplayName(),
                                  launchErr.isEmpty() ? SwString("The application could not be launched.")
                                                      : launchErr);
        }
    }

    wizard_ = nullptr;
    welcomePage_ = nullptr;
    targetPage_ = nullptr;
    componentsPage_ = nullptr;
    progressPage_ = nullptr;
    finishPage_ = nullptr;
    return (res == SwDialog::Accepted && lastResult_.ok) ? 0 : 1;
}

inline void SwInstallerWizardApp::onWizardPageChanged_(int currentId) {
    if (currentId != 3) {
        progressTaskPosted_ = false;
        return;
    }

    if (!wizard_ || !progressPage_) {
        return;
    }

    if (installInProgress_) {
        return;
    }
    if (lastResult_.ok) {
        if (auto* app = SwCoreApplication::instance(false)) {
            app->postEvent([this]() {
                if (wizard_) {
                    wizard_->next();
                }
            });
        } else {
            wizard_->next();
        }
        return;
    }
    if (progressTaskPosted_) {
        return;
    }

    progressTaskPosted_ = true;
    progressPage_->reset();
    progressPage_->setStatus("Preparing installer plan...");
    progressPage_->setProgress(8);
    progressPage_->appendLog("Collecting selected components and target paths.");

    if (auto* app = SwCoreApplication::instance(false)) {
        app->postEvent([this]() { runWizardInstall_(); });
    } else {
        runWizardInstall_();
    }
}

inline void SwInstallerWizardApp::runWizardInstall_() {
    progressTaskPosted_ = false;
    if (!wizard_ || !progressPage_ || !targetPage_ || !componentsPage_ || !finishPage_) {
        return;
    }

    const SwString installRoot = targetPage_->installRoot();
    const SwList<SwString> componentIds = componentsPage_->selectedComponents();
    if (installRoot.isEmpty()) {
        progressPage_->setStatus("Installation folder is required.");
        progressPage_->appendLog("The target path is empty.");
        progressPage_->setProgress(0);
        wizard_->back();
        return;
    }
    if (componentIds.isEmpty()) {
        progressPage_->setStatus("Select at least one component.");
        progressPage_->appendLog("No component was selected.");
        progressPage_->setProgress(0);
        wizard_->back();
        return;
    }

    installInProgress_ = true;
    lastResult_ = SwInstallerExecutionResult();
    activePlan_ = SwInstallerPlan();

    progressPage_->setStatus("Building declarative installer plan...");
    progressPage_->setProgress(18);
    progressPage_->appendLog("Creating the serialized install or repair plan.");

    activePlan_ = engine_.planInstall(installRoot, componentIds, false);

    progressPage_->setStatus("Executing installer runtime...");
    progressPage_->setProgress(36);
    progressPage_->appendLog(SwInstallerWindows::isProcessElevated()
                                 ? SwString("Already elevated; executing the plan in-process.")
                                 : SwString("Requesting elevation and executing the serialized plan."));

    SwString err;
    if (!runPlanWithElevation_(activePlan_, false, lastResult_, &err)) {
        lastResult_.ok = false;
        if (lastResult_.message.isEmpty()) {
            lastResult_.message = err.isEmpty() ? SwString("Installer execution failed") : err;
        }
    }

    installInProgress_ = false;
    progressPage_->setProgress(100);

    if (!lastResult_.ok) {
        progressPage_->setStatus("Installation failed.");
        progressPage_->appendLog(lastResult_.message.isEmpty() ? SwString("Unknown installer error.")
                                                               : lastResult_.message);
        finishPage_->setSummary("The installer failed. Use Back to review the target folder or "
                                "component selection, then run the plan again.");
        return;
    }

    progressPage_->setStatus("Installation completed.");
    progressPage_->appendLog(lastResult_.message.isEmpty() ? SwString("Installer completed successfully.")
                                                           : lastResult_.message);

    finishPage_->setSummary(SwString("Installed ") + product_.effectiveDisplayName() +
                            SwString(" to:\n") + activePlan_.installRoot);
    finishPage_->setLaunchVisible(!activePlan_.finalLaunches.isEmpty());
    finishPage_->setLaunchChecked(true);

    wizard_->next();
}

inline int SwInstallerWizardApp::runRepairInteractive_(SwWidget* parent, bool allowPostInstallLaunch) {
    if (SwMessageBox::question(parent,
                               product_.effectiveDisplayName(),
                               "Run a repair or refresh of the installed product?",
                               SwMessageBox::Yes | SwMessageBox::No) != SwMessageBox::Yes) {
        return 1;
    }
    SwInstallerPlan plan = engine_.planRepair(false);
    SwInstallerExecutionResult result;
    SwString err;
    if (!runPlanWithElevation_(plan, allowPostInstallLaunch, result, &err)) {
        SwMessageBox::critical(parent,
                               product_.effectiveDisplayName(),
                               err.isEmpty() ? SwString("Repair failed.") : err);
        return 1;
    }
    SwMessageBox::information(parent,
                              product_.effectiveDisplayName(),
                              result.message.isEmpty() ? SwString("Repair completed.") : result.message);
    return result.ok ? 0 : 1;
}

inline int SwInstallerWizardApp::runRepairSilent_(bool allowPostInstallLaunch) {
    SwInstallerPlan plan = engine_.planRepair(true);
    SwInstallerExecutionResult result;
    SwString err;
    if (!runPlanWithElevation_(plan, allowPostInstallLaunch, result, &err)) {
        return 1;
    }
    return result.ok ? 0 : 1;
}

inline int SwInstallerWizardApp::runUninstallInteractive_(SwWidget* parent, bool allowPostInstallLaunch) {
    SW_UNUSED(allowPostInstallLaunch)
    if (SwMessageBox::question(parent,
                               product_.effectiveDisplayName(),
                               "Remove the installed product from this machine?",
                               SwMessageBox::Yes | SwMessageBox::No) != SwMessageBox::Yes) {
        return 1;
    }
    SwInstallerPlan plan = engine_.planUninstall(false);
    SwInstallerExecutionResult result;
    SwString err;
    if (!runPlanWithElevation_(plan, false, result, &err)) {
        SwMessageBox::critical(parent,
                               product_.effectiveDisplayName(),
                               err.isEmpty() ? SwString("Uninstall failed.") : err);
        return 1;
    }
    SwMessageBox::information(parent,
                              product_.effectiveDisplayName(),
                              result.message.isEmpty() ? SwString("Uninstall completed.") : result.message);
    return result.ok ? 0 : 1;
}

inline int SwInstallerWizardApp::runUninstallSilent_(bool allowPostInstallLaunch) {
    SW_UNUSED(allowPostInstallLaunch)
    SwInstallerPlan plan = engine_.planUninstall(true);
    SwInstallerExecutionResult result;
    SwString err;
    if (!runPlanWithElevation_(plan, false, result, &err)) {
        return 1;
    }
    return result.ok ? 0 : 1;
}

inline bool SwInstallerWizardApp::runPlanWithElevation_(const SwInstallerPlan& plan,
                                                        bool allowPostInstallLaunch,
                                                        SwInstallerExecutionResult& result,
                                                        SwString* errOut) {
    if (SwInstallerWindows::isProcessElevated()) {
        SwString execErr;
        result = engine_.execute(plan, allowPostInstallLaunch, &execErr);
        if (!result.ok && result.message.isEmpty()) {
            result.message = execErr;
        }
        if (!result.ok && errOut) {
            *errOut = result.message;
        }
        return result.ok;
    }

    const SwString token = uniqueToken_();
    const SwString tempRoot = temporaryRoot_();
    if (!SwDir::mkpathAbsolute(tempRoot, false)) {
        if (errOut) *errOut = SwString("failed to create temporary directory: ") + tempRoot;
        return false;
    }

    const SwString planPath = normalizePath_(tempRoot + "/" + token + ".plan.json");
    const SwString resultPath = normalizePath_(tempRoot + "/" + token + ".result.json");

    SwString writeErr;
    if (!writeTextFile_(planPath, plan.toJson(), &writeErr)) {
        if (errOut) *errOut = writeErr;
        return false;
    }

    SwList<SwString> arguments;
    arguments.append("--execute-plan");
    arguments.append(planPath);
    arguments.append("--result-file");
    arguments.append(resultPath);
    if (allowPostInstallLaunch) {
        arguments.append("--launch-post-install=1");
    }

    unsigned long exitCode = 1;
    const SwString workingDirectory = parentPath_(setupSelfPath_);
    const bool launched = SwInstallerWindows::launchElevatedAndWait(setupSelfPath_,
                                                                    arguments,
                                                                    workingDirectory,
                                                                    exitCode,
                                                                    errOut);
    if (!launched) {
        (void)SwInstallerWindows::deleteFileOrScheduleReboot(planPath);
        (void)SwInstallerWindows::deleteFileOrScheduleReboot(resultPath);
        return false;
    }

    SwString resultErr;
    const SwString resultJson = readTextFile_(resultPath, &resultErr);
    if (resultJson.isEmpty() && !SwFile::exists(resultPath)) {
        if (errOut) {
            *errOut = resultErr.isEmpty()
                          ? (SwString("installer child did not produce a result file, exit code=") +
                             SwString::number(static_cast<long long>(exitCode)))
                          : resultErr;
        }
        (void)SwInstallerWindows::deleteFileOrScheduleReboot(planPath);
        (void)SwInstallerWindows::deleteFileOrScheduleReboot(resultPath);
        return false;
    }

    SwString parseErr;
    result = SwInstallerExecutionResult::fromJson(resultJson, &parseErr);
    (void)SwInstallerWindows::deleteFileOrScheduleReboot(planPath);
    (void)SwInstallerWindows::deleteFileOrScheduleReboot(resultPath);
    if (!parseErr.isEmpty()) {
        if (errOut) *errOut = parseErr;
        return false;
    }

    if (!result.ok && errOut) {
        *errOut = result.message.isEmpty()
                      ? (SwString("installer child failed, exit code=") +
                         SwString::number(static_cast<long long>(exitCode)))
                      : result.message;
    }
    return result.ok;
}

} // namespace swinstaller
