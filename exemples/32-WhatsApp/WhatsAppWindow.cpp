#include "WhatsAppWindow.h"

#include "SwDir.h"
#include "SwWidgetSnapshot.h"

#include "WhatsAppWidget.h"

// For snapshot media assets.
#include "graphics/SwImage.h"

#include <algorithm>

static SwString normalizeOutDir_(SwString dir) {
    if (dir.isEmpty()) {
        return dir;
    }
    if (!dir.endsWith("/") && !dir.endsWith("\\")) {
        dir.append("/");
    }
    return dir;
}

WhatsAppWindow::WhatsAppWindow()
    : SwMainWindow(L"WhatsApp", 1560, 820) {
    setStyleSheet("SwMainWindow { background-color: rgb(240, 242, 245); }");
    stripChrome_();
    ensureRoot_();
}

bool WhatsAppWindow::saveSnapshot(const SwString& outDir) {
    const SwString dir = normalizeOutDir_(outDir);
    if (dir.isEmpty()) {
        return false;
    }

    ensureRoot_(dir);
    if (!m_root) {
        return false;
    }

    bool ok = true;

    m_root->setLoggedInForSnapshot(false);
    ok = ok && SwWidgetSnapshot::savePng(m_root, dir + "wa_login.png");

    m_root->setLoggedInForSnapshot(true);
    // Keep the same name as the previous ref snapshot for easy diff.
    ok = ok && SwWidgetSnapshot::savePng(m_root, dir + "mvc_list.png");

    m_root->setAttachMenuForSnapshot(true);
    ok = ok && SwWidgetSnapshot::savePng(m_root, dir + "wa_attach_menu.png");
    m_root->setAttachMenuForSnapshot(false);

    // Media + multiline composer snapshots.
    SwDir::mkpathAbsolute(dir + "media");

    // Deterministic sample image (BMP) so the delegate can load it from the local DB root.
    {
        auto pack = [](int a, int r, int g, int b) -> std::uint32_t {
            const std::uint32_t aa = static_cast<std::uint32_t>(a & 0xFF);
            const std::uint32_t rr = static_cast<std::uint32_t>(r & 0xFF);
            const std::uint32_t gg = static_cast<std::uint32_t>(g & 0xFF);
            const std::uint32_t bb = static_cast<std::uint32_t>(b & 0xFF);
            return (aa << 24) | (rr << 16) | (gg << 8) | bb;
        };

        SwImage sample(640, 360, SwImage::Format_ARGB32);
        for (int y = 0; y < sample.height(); ++y) {
            std::uint32_t* row = sample.scanLine(y);
            if (!row) continue;
            for (int x = 0; x < sample.width(); ++x) {
                const int r = 20 + (x * 60) / std::max(1, sample.width() - 1);
                const int g = 60 + (y * 80) / std::max(1, sample.height() - 1);
                const int b = 120;
                row[x] = pack(255, r, g, b);
            }
        }
        // A bright "subject" rectangle.
        for (int y = 80; y < 280; ++y) {
            std::uint32_t* row = sample.scanLine(y);
            if (!row) continue;
            for (int x = 140; x < 500; ++x) {
                row[x] = pack(255, 245, 245, 245);
            }
        }

        sample.save(dir + "media/sample.bmp");
    }

    m_root->appendMediaMessageForSnapshot("image", "media/sample.bmp");
    m_root->appendMediaMessageForSnapshot("video", "media/sample.mp4");
    ok = ok && SwWidgetSnapshot::savePng(m_root, dir + "wa_media_messages.png");

    m_root->setComposerTextForSnapshot("Ligne 1\nLigne 2\nLigne 3\nLigne 4\nLigne 5\nLigne 6");
    ok = ok && SwWidgetSnapshot::savePng(m_root, dir + "wa_composer_multiline.png");

    m_root->setComposerTextForSnapshot(
        "Message très long pour valider le word-wrap et l'auto-grow du composer. "
        "Le champ doit passer de 1 à N lignes et pousser vers le haut, comme WhatsApp. "
        "Message très long pour valider le word-wrap et l'auto-grow du composer. "
        "Le champ doit passer de 1 à N lignes et pousser vers le haut, comme WhatsApp. "
        "Message très long pour valider le word-wrap et l'auto-grow du composer. "
        "Le champ doit passer de 1 à N lignes et pousser vers le haut, comme WhatsApp. "
        "Message très long pour valider le word-wrap et l'auto-grow du composer. "
        "Le champ doit passer de 1 à N lignes et pousser vers le haut, comme WhatsApp. "
        "Message très long pour valider le word-wrap et l'auto-grow du composer. "
        "Le champ doit passer de 1 à N lignes et pousser vers le haut, comme WhatsApp.");
    ok = ok && SwWidgetSnapshot::savePng(m_root, dir + "wa_composer_wrap.png");

    // Thread: long message wrapping + selection (read-only).
    const SwString longText =
        "Un message tres long envoye dans le thread doit etre word-wrap, sans etre tronque. "
        "On doit pouvoir selectionner une partie du texte meme si la bulle est en read-only. "
        "Un message tres long envoye dans le thread doit etre word-wrap, sans etre tronque. "
        "On doit pouvoir selectionner une partie du texte meme si la bulle est en read-only. "
        "Un message tres long envoye dans le thread doit etre word-wrap, sans etre tronque. "
        "On doit pouvoir selectionner une partie du texte meme si la bulle est en read-only.";

    m_root->appendTextMessageForSnapshot(longText);
    ok = ok && SwWidgetSnapshot::savePng(m_root, dir + "wa_thread_long_text.png");

    m_root->setLastTextSelectionForSnapshot(10, 120);
    ok = ok && SwWidgetSnapshot::savePng(m_root, dir + "wa_thread_text_selection.png");

    return ok;
}

void WhatsAppWindow::resizeEvent(ResizeEvent* event) {
    SwMainWindow::resizeEvent(event);
    if (!m_root) {
        return;
    }
    const SwRect r = getRect();
    m_root->move(r.x, r.y);
    m_root->resize(r.width, r.height);
}

void WhatsAppWindow::ensureRoot_(const SwString& storageRoot) {
    if (m_root) {
        if (!storageRoot.isEmpty() && storageRoot != m_rootStorageRoot) {
            delete m_root;
            m_root = nullptr;
        } else {
            return;
        }
    }

    m_rootStorageRoot = storageRoot;
    m_root = new WhatsAppWidget(this, m_rootStorageRoot);
    const SwRect r = getRect();
    m_root->move(r.x, r.y);
    m_root->resize(r.width, r.height);
}

void WhatsAppWindow::stripChrome_() {
    // SwMainWindow always creates Qt-like chrome (menu/tool/status bars). We hide them
    // and detach its internal layout so this window becomes a clean canvas.
    if (menuBar()) {
        menuBar()->hide();
    }
    if (toolBar()) {
        toolBar()->hide();
    }
    if (statusBar()) {
        statusBar()->hide();
    }
    if (centralWidget()) {
        centralWidget()->hide();
    }

    // Detach the chrome layout (it reserves space even when the bars are hidden).
    SwWidget::setLayout(nullptr);
}
