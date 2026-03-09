#include "SwGuiApplication.h"
#include "SwMainWindow.h"
#include "SwPushButton.h"
#include "SwFileDialog.h"
#include "SwColorDialog.h"
#include "SwFontDialog.h"
#include "SwMessageBox.h"
#include "SwInputDialog.h"
#include "SwProgressDialog.h"
#include "SwErrorMessage.h"
#include "SwWizard.h"
#include "SwStatusBar.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwLineEdit.h"
#include "SwTimer.h"
#include "SwString.h"

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#include <fstream>
#include <ctime>
#pragma comment(lib, "dbghelp.lib")

static LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep) {
    // Write minidump
    HANDLE hFile = CreateFileW(L"C:\\Users\\eymer\\crash.dmp",
        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
            MiniDumpWithFullMemory, &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }
    // Write text log
    std::ofstream log("C:\\Users\\eymer\\crash.txt");
    if (log.is_open()) {
        log << "EXCEPTION CODE: 0x" << std::hex << ep->ExceptionRecord->ExceptionCode << "\n";
        log << "EXCEPTION ADDRESS: 0x" << ep->ExceptionRecord->ExceptionAddress << "\n";
        // Walk stack
        HANDLE hProcess = GetCurrentProcess();
        HANDLE hThread = GetCurrentThread();
        SymInitialize(hProcess, nullptr, TRUE);
        CONTEXT ctx = *ep->ContextRecord;
        STACKFRAME64 sf = {};
        sf.AddrPC.Mode = AddrModeFlat;
        sf.AddrFrame.Mode = AddrModeFlat;
        sf.AddrStack.Mode = AddrModeFlat;
#ifdef _M_X64
        sf.AddrPC.Offset    = ctx.Rip;
        sf.AddrFrame.Offset = ctx.Rbp;
        sf.AddrStack.Offset = ctx.Rsp;
        const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#else
        sf.AddrPC.Offset    = ctx.Eip;
        sf.AddrFrame.Offset = ctx.Ebp;
        sf.AddrStack.Offset = ctx.Esp;
        const DWORD machineType = IMAGE_FILE_MACHINE_I386;
#endif
        char symBuf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        for (int i = 0; i < 32; ++i) {
            if (!StackWalk64(machineType, hProcess, hThread, &sf, &ctx,
                             nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                break;
            if (sf.AddrPC.Offset == 0) break;
            DWORD64 disp = 0;
            if (SymFromAddr(hProcess, sf.AddrPC.Offset, &disp, sym)) {
                log << "  " << sym->Name << " + 0x" << std::hex << disp
                    << " [0x" << sf.AddrPC.Offset << "]\n";
            } else {
                log << "  [0x" << std::hex << sf.AddrPC.Offset << "]\n";
            }
        }
        SymCleanup(hProcess);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static SwString exampleDialogInitialPath() {
#if defined(_WIN32)
    return "C:\\";
#else
    return SwString();
#endif
}

int main() {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(crashHandler);
#endif
    SwGuiApplication app;
    SwMainWindow mainWindow("SwStack Demo");
    mainWindow.resize(640, 400);

    // Build menu bar
    SwMenuBar* menuBar = mainWindow.menuBar();

    SwMenu* fileMenu   = menuBar->addMenu("File");
    SwMenu* dialogMenu = menuBar->addMenu("Dialogs");
    SwMenu* editMenu   = menuBar->addMenu("Edit");

    // Central widget is the usable area below the menu bar
    SwWidget* central = mainWindow.centralWidget();

    SwString btnStyle = R"(
        SwPushButton {
            background-color: rgb(56, 118, 255);
            color: #FFFFFF;
            border-radius: 12px;
            border-width: 0px;
            padding: 6px 14px;
        }
    )";

    // ── Result label ──
    SwLabel* resultLabel = new SwLabel(central);
    resultLabel->setText("Click a button or use the menus to open a dialog");
    resultLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(40,40,40); font-size: 13px; }");

    // ── Row 1 — File dialogs ──
    SwWidget* row1 = new SwWidget(central);
    SwPushButton* btnOpen = new SwPushButton("Open File...", row1);
    btnOpen->setStyleSheet(btnStyle);
    SwPushButton* btnDir = new SwPushButton("Select Folder...", row1);
    btnDir->setStyleSheet(btnStyle);

    SwHorizontalLayout* row1Layout = new SwHorizontalLayout();
    row1Layout->setSpacing(10);
    row1Layout->setMargin(0);
    row1Layout->addWidget(btnOpen,  1);
    row1Layout->addWidget(btnDir,   1);
    row1->setLayout(row1Layout);

    // ── Row 2 — Color, Font, MessageBox ──
    SwWidget* row2 = new SwWidget(central);
    SwPushButton* btnColor = new SwPushButton("Pick Color...", row2);
    btnColor->setStyleSheet(btnStyle);
    SwPushButton* btnFont = new SwPushButton("Pick Font...", row2);
    btnFont->setStyleSheet(btnStyle);
    SwPushButton* btnMsg = new SwPushButton("Message Box...", row2);
    btnMsg->setStyleSheet(btnStyle);

    SwHorizontalLayout* row2Layout = new SwHorizontalLayout();
    row2Layout->setSpacing(10);
    row2Layout->setMargin(0);
    row2Layout->addWidget(btnColor, 1);
    row2Layout->addWidget(btnFont,  1);
    row2Layout->addWidget(btnMsg,   1);
    row2->setLayout(row2Layout);

    // ── Row 3 — Warning, Critical, Information ──
    SwWidget* row3 = new SwWidget(central);
    SwPushButton* btnWarn = new SwPushButton("Warning...", row3);
    btnWarn->setStyleSheet(btnStyle);
    SwPushButton* btnCrit = new SwPushButton("Critical...", row3);
    btnCrit->setStyleSheet(btnStyle);
    SwPushButton* btnInfo = new SwPushButton("Information...", row3);
    btnInfo->setStyleSheet(btnStyle);

    SwHorizontalLayout* row3Layout = new SwHorizontalLayout();
    row3Layout->setSpacing(10);
    row3Layout->setMargin(0);
    row3Layout->addWidget(btnWarn, 1);
    row3Layout->addWidget(btnCrit, 1);
    row3Layout->addWidget(btnInfo, 1);
    row3->setLayout(row3Layout);

    // ── Row 4 — Input, Progress, Error, Wizard ──
    SwWidget* row4 = new SwWidget(central);
    SwPushButton* btnInput = new SwPushButton("Input...", row4);
    btnInput->setStyleSheet(btnStyle);
    SwPushButton* btnProgress = new SwPushButton("Progress...", row4);
    btnProgress->setStyleSheet(btnStyle);
    SwPushButton* btnError = new SwPushButton("Error Msg...", row4);
    btnError->setStyleSheet(btnStyle);
    SwPushButton* btnWizard = new SwPushButton("Wizard...", row4);
    btnWizard->setStyleSheet(btnStyle);

    SwHorizontalLayout* row4Layout = new SwHorizontalLayout();
    row4Layout->setSpacing(10);
    row4Layout->setMargin(0);
    row4Layout->addWidget(btnInput,    1);
    row4Layout->addWidget(btnProgress, 1);
    row4Layout->addWidget(btnError,    1);
    row4Layout->addWidget(btnWizard,   1);
    row4->setLayout(row4Layout);

    // ── Main vertical layout ──
    SwVerticalLayout* mainLayout = new SwVerticalLayout();
    mainLayout->setSpacing(10);
    mainLayout->setMargin(20);
    mainLayout->addWidget(resultLabel, 0, 26);
    mainLayout->addWidget(row1,        0, 40);
    mainLayout->addWidget(row2,        0, 40);
    mainLayout->addWidget(row3,        0, 40);
    mainLayout->addWidget(row4,        0, 40);
    central->setLayout(mainLayout);

    // --- Lambdas ---
    auto openFile = [&]() {
        SwString f = SwFileDialog::getOpenFileName(&mainWindow,
                                                   "Open File",
                                                   exampleDialogInitialPath(),
                                                   "All Files (*.*);;Text Files (*.txt);;Images (*.png *.jpg)");
        if (!f.isEmpty()) {
            resultLabel->setText(SwString("File: ") + f);
        }
    };

    auto openDir = [&]() {
        SwString d = SwFileDialog::getExistingDirectory(&mainWindow,
                                                        "Select Folder",
                                                        exampleDialogInitialPath());
        if (!d.isEmpty()) {
            resultLabel->setText(SwString("Folder: ") + d);
        }
    };

    auto pickColor = [&]() {
        bool ok = false;
        SwColor c = SwColorDialog::getColor(SwColor{255, 255, 255}, &mainWindow, &ok, "Pick a Color");
        if (ok) {
            resultLabel->setText(SwString("Color: rgb(")
                + SwString::number(c.r) + ", "
                + SwString::number(c.g) + ", "
                + SwString::number(c.b) + ")");
        }
    };

    auto pickFont = [&]() {
        bool ok = false;
        SwFont f = SwFontDialog::getFont(SwFont(L"Arial", 12), &mainWindow, &ok, "Pick a Font");
        if (ok) {
            resultLabel->setText(SwString("Font: ") + SwString::fromWString(f.getFamily())
                + " " + SwString::number(f.getPointSize()) + "pt");
        }
    };

    auto showMessage = [&]() {
        int ret = SwMessageBox::question(&mainWindow,
                                         "Question",
                                         "Do you like SwStack?",
                                         SwMessageBox::Yes | SwMessageBox::No);
        if (ret == SwMessageBox::Yes) {
            SwMessageBox box(&mainWindow);
            box.setWindowTitle("Answer");
            box.setText("Great! :)");
            box.exec();
        } else {
            SwMessageBox box(&mainWindow);
            box.setWindowTitle("Answer");
            box.setText("Maybe next time!");
            box.exec();
        }
    };

    auto showWarning = [&]() {
        SwMessageBox::warning(&mainWindow, "Warning", "This is a warning message.");
    };

    auto showCritical = [&]() {
        SwMessageBox::critical(&mainWindow, "Critical Error", "Something went seriously wrong!");
    };

    auto showInfo = [&]() {
        SwMessageBox::information(&mainWindow, "Information", "This is an informational message.");
    };

    auto showInput = [&]() {
        bool ok = false;
        SwString name = SwInputDialog::getText(&mainWindow, "Input", "Enter your name:", "World", &ok);
        if (ok) {
            resultLabel->setText(SwString("Hello, ") + name + "!");
        }
    };

    auto showProgress = [&]() {
        SwProgressDialog dlg("Processing files...", "Cancel", 0, 100, &mainWindow);
        dlg.setWindowTitle("Progress");
        dlg.setValue(0);

        SwTimer timer;
        timer.setInterval(80);
        SwObject::connect(&timer, &SwTimer::timeout, [&]() {
            if (dlg.wasCanceled()) {
                timer.stop();
                return;
            }
            int v = dlg.value() + 1;
            dlg.setValue(v);
            dlg.setLabelText(SwString("Processing... ") + SwString::number(v) + "%");
            if (v >= 100) {
                timer.stop();
                dlg.accept();
            }
        });
        timer.start();
        int res = dlg.exec();
        timer.stop();
        if (res == SwDialog::Accepted) {
            resultLabel->setText("Progress completed!");
        } else {
            resultLabel->setText("Progress cancelled.");
        }
    };

    auto showError = [&]() {
        SwErrorMessage errDlg(&mainWindow);
        errDlg.showMessage("Something unexpected happened!");
    };

    auto showWizard = [&]() {
        SwWizard wiz(&mainWindow);
        wiz.setWindowTitle("Setup Wizard");

        auto* page1 = new SwWizardPage();
        page1->setTitle("Welcome");
        page1->setSubTitle("Get started with the setup process");
        auto* welcomeLabel = new SwLabel("Welcome to the SwStack wizard demo.\nClick Next to continue.", page1);
        welcomeLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(24,28,36); font-size: 14px; }");
        welcomeLabel->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak));
        welcomeLabel->move(0, 0);
        welcomeLabel->resize(380, 80);

        auto* page2 = new SwWizardPage();
        page2->setTitle("Configuration");
        page2->setSubTitle("Configure your project settings");
        auto* cfgLabel = new SwLabel("Project name:", page2);
        cfgLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(24,28,36); font-size: 14px; }");
        cfgLabel->move(0, 0);
        cfgLabel->resize(440, 24);
        auto* cfgEdit = new SwLineEdit(page2);
        cfgEdit->setText("MyProject");
        cfgEdit->move(0, 28);
        cfgEdit->resize(300, 34);

        auto* page3 = new SwWizardPage();
        page3->setTitle("Finish");
        page3->setSubTitle("Review and complete");
        auto* finishLabel = new SwLabel("Setup is complete.\nClick Finish to close.", page3);
        finishLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(24,28,36); font-size: 14px; }");
        finishLabel->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak));
        finishLabel->move(0, 0);
        finishLabel->resize(440, 80);

        wiz.addPage(page1);
        wiz.addPage(page2);
        wiz.addPage(page3);

        int res = wiz.exec();
        if (res == SwDialog::Accepted) {
            resultLabel->setText("Wizard completed!");
        } else {
            resultLabel->setText("Wizard cancelled.");
        }
    };

    // Connect buttons
    SwObject::connect(btnOpen,  &SwPushButton::clicked, openFile);
    SwObject::connect(btnDir,   &SwPushButton::clicked, openDir);
    SwObject::connect(btnColor, &SwPushButton::clicked, pickColor);
    SwObject::connect(btnFont,  &SwPushButton::clicked, pickFont);
    SwObject::connect(btnMsg,   &SwPushButton::clicked, showMessage);
    SwObject::connect(btnWarn,  &SwPushButton::clicked, showWarning);
    SwObject::connect(btnCrit,  &SwPushButton::clicked, showCritical);
    SwObject::connect(btnInfo,  &SwPushButton::clicked, showInfo);
    SwObject::connect(btnInput, &SwPushButton::clicked, showInput);
    SwObject::connect(btnProgress, &SwPushButton::clicked, showProgress);
    SwObject::connect(btnError, &SwPushButton::clicked, showError);
    SwObject::connect(btnWizard, &SwPushButton::clicked, showWizard);

    // Wire File menu actions
    fileMenu->addAction("Open File...", openFile);
    fileMenu->addAction("Open Folder...", openDir);
    fileMenu->addSeparator();
    fileMenu->addAction("Exit", [&]() {
        if (auto* guiApp = SwGuiApplication::instance(false)) {
            guiApp->exit(0);
        }
    });

    // Wire Dialogs menu
    dialogMenu->addAction("Pick Color...", pickColor);
    dialogMenu->addAction("Pick Font...", pickFont);
    dialogMenu->addSeparator();
    dialogMenu->addAction("Message Box...", showMessage);
    dialogMenu->addAction("Warning...", showWarning);
    dialogMenu->addAction("Critical...", showCritical);
    dialogMenu->addAction("Information...", showInfo);
    dialogMenu->addSeparator();
    dialogMenu->addAction("Input...", showInput);
    dialogMenu->addAction("Progress...", showProgress);
    dialogMenu->addAction("Error Message...", showError);
    dialogMenu->addAction("Wizard...", showWizard);

    // Edit menu (placeholder actions)
    editMenu->addAction("Cut");
    editMenu->addAction("Copy");
    editMenu->addAction("Paste");

    // Status bar
    SwStatusBar* statusBar = mainWindow.statusBar();
    statusBar->showMessage("Ready  |  SwStack Demo  |  12 dialogs available");

    mainWindow.show();
    return app.exec();
}
