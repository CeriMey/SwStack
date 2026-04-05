#pragma once

/**
 * @file src/core/installer/SwInstallerWindows.h
 * @brief Header-only Windows integration helpers used by the Sw installer runtime.
 */

#include "SwDir.h"
#include "SwFile.h"
#include "SwFileInfo.h"
#include "SwList.h"
#include "SwString.h"

#include <cstdio>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <KnownFolders.h>
#include <Aclapi.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

namespace swinstaller {

class SwInstallerWindows {
public:
#if defined(_WIN32)
    struct MutexHandle {
        HANDLE handle{nullptr};
        bool alreadyExists{false};

        ~MutexHandle() {
            if (handle) {
                ::CloseHandle(handle);
            }
        }
    };
#endif

    static SwString currentExecutablePath() {
#if defined(_WIN32)
        wchar_t buffer[MAX_PATH];
        const DWORD len = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (len == 0) {
            return SwString();
        }
        SwString path = SwString::fromWCharArray(buffer);
        path.replace("\\", "/");
        return path;
#else
        return SwString();
#endif
    }

    static SwString programFilesDir() {
#if defined(_WIN32)
        return knownFolder_(FOLDERID_ProgramFiles);
#else
        return SwString();
#endif
    }

    static SwString programDataDir() {
#if defined(_WIN32)
        return knownFolder_(FOLDERID_ProgramData);
#else
        return SwString();
#endif
    }

    static SwString commonDesktopDir() {
#if defined(_WIN32)
        return knownFolder_(FOLDERID_PublicDesktop);
#else
        return SwString();
#endif
    }

    static SwString userDesktopDir() {
#if defined(_WIN32)
        return knownFolder_(FOLDERID_Desktop);
#else
        return SwString();
#endif
    }

    static SwString commonProgramsDir() {
#if defined(_WIN32)
        return knownFolder_(FOLDERID_CommonPrograms);
#else
        return SwString();
#endif
    }

    static SwString userProgramsDir() {
#if defined(_WIN32)
        return knownFolder_(FOLDERID_Programs);
#else
        return SwString();
#endif
    }

    static SwString makeUninstallRegistryKey(const SwString& productId) {
        return SwString("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + productId;
    }

    static bool isProcessElevated() {
#if defined(_WIN32)
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
            return false;
        }
        TOKEN_ELEVATION elevation{};
        DWORD bytes = 0;
        const BOOL ok =
            ::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes);
        ::CloseHandle(token);
        return ok && elevation.TokenIsElevated;
#else
        return false;
#endif
    }

    static bool createSingleInstanceMutex(const SwString& productId,
#if defined(_WIN32)
                                          MutexHandle& outHandle,
#else
                                          int&,
#endif
                                          SwString* errOut = nullptr) {
#if defined(_WIN32)
        const std::wstring name =
            (SwString("Local\\") + productId + SwString("_InstallerMutex")).toStdWString();
        outHandle.handle = ::CreateMutexW(nullptr, FALSE, name.c_str());
        if (!outHandle.handle) {
            if (errOut) {
                *errOut = SwString("CreateMutexW failed for product: ") + productId;
            }
            return false;
        }
        outHandle.alreadyExists = (::GetLastError() == ERROR_ALREADY_EXISTS);
        return true;
#else
        (void)productId;
        if (errOut) {
            *errOut = "single instance mutex is only available on Windows";
        }
        return false;
#endif
    }

    static bool launchElevatedAndWait(const SwString& executablePath,
                                      const SwList<SwString>& arguments,
                                      const SwString& workingDirectory,
                                      unsigned long& exitCode,
                                      SwString* errOut = nullptr) {
#if defined(_WIN32)
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        const std::wstring exe = executablePath.toStdWString();
        const std::wstring args = joinCommandLine_(arguments);
        const std::wstring cwd = workingDirectory.toStdWString();
        sei.lpFile = exe.c_str();
        sei.lpParameters = args.c_str();
        sei.lpDirectory = cwd.empty() ? nullptr : cwd.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (!::ShellExecuteExW(&sei)) {
            if (errOut) {
                *errOut = SwString("ShellExecuteExW runas failed for: ") + executablePath;
            }
            return false;
        }
        ::WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD code = 1;
        (void)::GetExitCodeProcess(sei.hProcess, &code);
        ::CloseHandle(sei.hProcess);
        exitCode = code;
        return true;
#else
        (void)executablePath;
        (void)arguments;
        (void)workingDirectory;
        exitCode = 1;
        if (errOut) {
            *errOut = "elevation is only available on Windows";
        }
        return false;
#endif
    }

    static bool runProcessAndWait(const SwString& executablePath,
                                  const SwList<SwString>& arguments,
                                  const SwString& workingDirectory,
                                  bool hiddenWindow,
                                  unsigned long& exitCode,
                                  SwString* errOut = nullptr) {
#if defined(_WIN32)
        std::wstring cmd = quoteArgument_(executablePath.toStdWString());
        const std::wstring joinedArgs = joinCommandLine_(arguments);
        if (!joinedArgs.empty()) {
            cmd += L" ";
            cmd += joinedArgs;
        }
        std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
        mutableCmd.push_back(L'\0');

        STARTUPINFOW si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = hiddenWindow ? SW_HIDE : SW_SHOWNORMAL;
        const std::wstring cwd = workingDirectory.toStdWString();
        if (!::CreateProcessW(nullptr,
                              mutableCmd.data(),
                              nullptr,
                              nullptr,
                              FALSE,
                              hiddenWindow ? CREATE_NO_WINDOW : 0,
                              nullptr,
                              cwd.empty() ? nullptr : cwd.c_str(),
                              &si,
                              &pi)) {
            if (errOut) {
                *errOut = SwString("CreateProcessW failed for: ") + executablePath;
            }
            return false;
        }

        ::WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        (void)::GetExitCodeProcess(pi.hProcess, &code);
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        exitCode = code;
        return true;
#else
        (void)executablePath;
        (void)arguments;
        (void)workingDirectory;
        (void)hiddenWindow;
        exitCode = 1;
        if (errOut) {
            *errOut = "process execution is only available on Windows";
        }
        return false;
#endif
    }

    static bool createShortcut(const SwString& linkPath,
                               const SwString& targetPath,
                               const SwString& arguments,
                               const SwString& workingDirectory,
                               const SwString& description,
                               const SwString& iconPath,
                               int iconIndex,
                               SwString* errOut = nullptr) {
#if defined(_WIN32)
        const SwString dirPath = parentPath_(linkPath);
        if (!dirPath.isEmpty() && !SwDir::mkpathAbsolute(dirPath, false)) {
            if (errOut) {
                *errOut = SwString("failed to create shortcut directory: ") + dirPath;
            }
            return false;
        }

        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool shouldUninit = SUCCEEDED(hr);
        IShellLinkW* shellLink = nullptr;
        hr = ::CoCreateInstance(CLSID_ShellLink,
                                nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW,
                                reinterpret_cast<void**>(&shellLink));
        if (FAILED(hr) || !shellLink) {
            if (shouldUninit) {
                ::CoUninitialize();
            }
            if (errOut) {
                *errOut = SwString("CoCreateInstance(IShellLink) failed for shortcut: ") + linkPath;
            }
            return false;
        }

        (void)shellLink->SetPath(targetPath.toStdWString().c_str());
        (void)shellLink->SetArguments(arguments.toStdWString().c_str());
        (void)shellLink->SetWorkingDirectory(workingDirectory.toStdWString().c_str());
        (void)shellLink->SetDescription(description.toStdWString().c_str());
        if (!iconPath.isEmpty()) {
            (void)shellLink->SetIconLocation(iconPath.toStdWString().c_str(), iconIndex);
        }

        IPersistFile* persistFile = nullptr;
        hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
        if (FAILED(hr) || !persistFile) {
            shellLink->Release();
            if (shouldUninit) {
                ::CoUninitialize();
            }
            if (errOut) {
                *errOut = SwString("QueryInterface(IPersistFile) failed for shortcut: ") + linkPath;
            }
            return false;
        }

        hr = persistFile->Save(linkPath.toStdWString().c_str(), TRUE);
        persistFile->Release();
        shellLink->Release();
        if (shouldUninit) {
            ::CoUninitialize();
        }

        if (FAILED(hr)) {
            if (errOut) {
                *errOut = SwString("saving shortcut failed: ") + linkPath;
            }
            return false;
        }
        return true;
#else
        (void)linkPath;
        (void)targetPath;
        (void)arguments;
        (void)workingDirectory;
        (void)description;
        (void)iconPath;
        (void)iconIndex;
        if (errOut) {
            *errOut = "shortcut creation is only available on Windows";
        }
        return false;
#endif
    }

    static bool writeUninstallEntry(const SwString& registryKeyPath,
                                    const SwString& displayName,
                                    const SwString& publisher,
                                    const SwString& version,
                                    const SwString& installLocation,
                                    const SwString& uninstallCommand,
                                    const SwString& displayIcon,
                                    SwString* errOut = nullptr) {
#if defined(_WIN32)
        HKEY key = nullptr;
        const std::wstring path = registryKeyPath.toStdWString();
        DWORD disposition = 0;
        const HKEY rootKey = isProcessElevated() ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        LONG status = ::RegCreateKeyExW(rootKey,
                                        path.c_str(),
                                        0,
                                        nullptr,
                                        REG_OPTION_NON_VOLATILE,
                                        KEY_WRITE,
                                        nullptr,
                                        &key,
                                        &disposition);
        if (status != ERROR_SUCCESS || !key) {
            if (errOut) {
                *errOut = SwString("failed to create uninstall registry key: ") + registryKeyPath;
            }
            return false;
        }

        const bool ok =
            writeRegString_(key, L"DisplayName", displayName) &&
            writeRegString_(key, L"Publisher", publisher) &&
            writeRegString_(key, L"DisplayVersion", version) &&
            writeRegString_(key, L"InstallLocation", installLocation) &&
            writeRegString_(key, L"UninstallString", uninstallCommand) &&
            writeRegString_(key, L"DisplayIcon", displayIcon) &&
            writeRegDword_(key, L"NoModify", 1) &&
            writeRegDword_(key, L"NoRepair", 0);

        ::RegCloseKey(key);
        if (!ok && errOut) {
            *errOut = SwString("failed to write uninstall registry values: ") + registryKeyPath;
        }
        return ok;
#else
        (void)registryKeyPath;
        (void)displayName;
        (void)publisher;
        (void)version;
        (void)installLocation;
        (void)uninstallCommand;
        (void)displayIcon;
        if (errOut) {
            *errOut = "uninstall registry integration is only available on Windows";
        }
        return false;
#endif
    }

    static bool removeUninstallEntry(const SwString& registryKeyPath, SwString* errOut = nullptr) {
#if defined(_WIN32)
        const HKEY rootKey = isProcessElevated() ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        const LONG status =
            ::RegDeleteTreeW(rootKey, registryKeyPath.toStdWString().c_str());
        if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        if (errOut) {
            *errOut = SwString("failed to delete uninstall registry key: ") + registryKeyPath;
        }
        return false;
#else
        (void)registryKeyPath;
        if (errOut) {
            *errOut = "uninstall registry integration is only available on Windows";
        }
        return false;
#endif
    }

    static bool deleteFileOrScheduleReboot(const SwString& path) {
#if defined(_WIN32)
        const std::wstring wide = path.toStdWString();
        if (::DeleteFileW(wide.c_str())) {
            return true;
        }
        const DWORD err = ::GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        return ::MoveFileExW(wide.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != 0;
#else
        return ::remove(path.toStdString().c_str()) == 0;
#endif
    }

    static bool deleteDirectoryIfEmpty(const SwString& path) {
#if defined(_WIN32)
        const std::wstring wide = path.toStdWString();
        if (::RemoveDirectoryW(wide.c_str())) {
            return true;
        }
        const DWORD err = ::GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND || err == ERROR_DIR_NOT_EMPTY;
#else
        return false;
#endif
    }

private:
#if defined(_WIN32)
    static SwString knownFolder_(const KNOWNFOLDERID& id) {
        PWSTR path = nullptr;
        if (FAILED(::SHGetKnownFolderPath(id, 0, nullptr, &path)) || !path) {
            return SwString();
        }
        SwString out = SwString::fromWCharArray(path);
        out.replace("\\", "/");
        ::CoTaskMemFree(path);
        return out;
    }

    static std::wstring quoteArgument_(const std::wstring& s) {
        if (s.empty()) {
            return L"\"\"";
        }
        const bool needsQuotes = (s.find_first_of(L" \t\n\v\"") != std::wstring::npos);
        if (!needsQuotes) {
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

    static std::wstring joinCommandLine_(const SwList<SwString>& arguments) {
        std::wstring joined;
        for (size_t i = 0; i < arguments.size(); ++i) {
            if (!joined.empty()) {
                joined.push_back(L' ');
            }
            joined += quoteArgument_(arguments[i].toStdWString());
        }
        return joined;
    }

    static bool writeRegString_(HKEY key, const wchar_t* valueName, const SwString& value) {
        const std::wstring wide = value.toStdWString();
        const DWORD bytes = static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t));
        return ::RegSetValueExW(key,
                                valueName,
                                0,
                                REG_SZ,
                                reinterpret_cast<const BYTE*>(wide.c_str()),
                                bytes) == ERROR_SUCCESS;
    }

    static bool writeRegDword_(HKEY key, const wchar_t* valueName, DWORD value) {
        return ::RegSetValueExW(key,
                                valueName,
                                0,
                                REG_DWORD,
                                reinterpret_cast<const BYTE*>(&value),
                                sizeof(value)) == ERROR_SUCCESS;
    }
#endif

    static SwString parentPath_(SwString path) {
        path.replace("\\", "/");
        const size_t slash = path.lastIndexOf('/');
        if (slash == static_cast<size_t>(-1)) {
            return SwString();
        }
        return path.left(static_cast<int>(slash));
    }
};

} // namespace swinstaller
