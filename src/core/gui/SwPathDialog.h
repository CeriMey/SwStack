#pragma once

/**
 * @file src/core/gui/SwPathDialog.h
 * @ingroup core_gui
 * @brief Declares `SwPathDialog`, a thin directory picker facade that normalizes paths for Sw.
 */

#include "SwDir.h"
#include "SwFileDialog.h"

class SwPathDialog {
public:
    static SwString normalizePath(SwString path) {
        if (path.isEmpty()) {
            return SwString();
        }
        path = SwDir::normalizePath(path.trimmed());
        path.replace("\\", "/");
        stripWindowsExtendedPrefix_(path);
        return path;
    }

    static SwString getExistingDirectory(SwWidget* parent,
                                         const SwString& caption = "Select directory",
                                         const SwString& dir = SwString()) {
        return normalizePath(
            SwFileDialog::getExistingDirectory(parent, caption, normalizePath(dir)));
    }

private:
    static void stripWindowsExtendedPrefix_(SwString& path) {
        while (path.startsWith("///?/")) {
            path.remove(0, 1);
        }
        if (path.startsWith("//?/UNC/")) {
            path.remove(0, 8);
            path = "//" + path;
            return;
        }
        while (path.startsWith("//?/")) {
            path.remove(0, 4);
        }
        while (path.startsWith("/?/")) {
            path.remove(0, 3);
        }
        while (path.startsWith("/") && path.size() > 2 && path[2] == ':') {
            path.remove(0, 1);
        }
    }
};
