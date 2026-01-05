#include "WhatsAppWidget.h"

#include "SwButton.h"
#include "SwFrame.h"
#include "SwLabel.h"
#include "SwLineEdit.h"
#include "SwListView.h"
#include "SwListWidget.h"
#include "SwStandardItemModel.h"

#include "SwDateTime.h"
#include "SwDialog.h"
#include "SwFileDialog.h"
#include "SwFileInfo.h"
#include "SwItemSelectionModel.h"
#include "SwSettings.h"
#include "SwStandardPaths.h"

#include "chatbubble/SwChatBubble.h"

#include "WaMediaViewerDialog.h"
#include "WaThreadBubbleDelegate.h"

#include "WaAvatarCircle.h"
#include "WaAttachMenuPopup.h"
#include "WaChatWallpaper.h"
#include "WaConversationListDelegate.h"
#include "WaConversationRowWidget.h"
#include "WaDemoAssets.h"
#include "WaEmojiPickerPopup.h"
#include "WaIconButton.h"
#include "WaLocalStore.h"
#include "WaLoginPage.h"
#include "WaMessageEdit.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

#include "fireBD/FireBDChatService.h"
#include "fireBD/FireBDUserService.h"

static int clampInt_(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static SwString twoDigits_(int v) {
    if (v < 0) v = 0;
    if (v < 10) {
        return "0" + SwString::number(v);
    }
    return SwString::number(v);
}

static SwString nowHm_() {
    SwDateTime now;
    return twoDigits_(now.hour()) + ":" + twoDigits_(now.minute());
}

static SwString envStr_(const char* name) {
    if (!name || !name[0]) {
        return SwString();
    }
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value) {
        return SwString();
    }
    SwString out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? SwString(value) : SwString();
#endif
}

static SwSettings& waSettings_() {
    static SwSettings* s = nullptr;
    if (!s) {
        const SwString profile = envStr_("SW_WA_PROFILE").trimmed();
        const SwString appName = profile.isEmpty() ? SwString("WhatsApp") : (SwString("WhatsApp_") + profile);
        s = new SwSettings("SwCore", appName);
    }
    return *s;
}

static SwString settingsStr_(const SwString& key) {
    return waSettings_().value(key).toString().trimmed();
}

static bool settingsBool_(const SwString& key, bool fallback) {
    return waSettings_().value(key, SwAny(fallback)).toBool();
}

static int settingsInt_(const SwString& key, int fallback) {
    return waSettings_().value(key, SwAny(fallback)).toInt();
}

static SwString sanitizePathComponent_(SwString s) {
    const std::string in = s.toStdString();
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        const bool ok = (std::isalnum(c) != 0) || c == '-' || c == '_' || c == '.';
        out.push_back(ok ? static_cast<char>(c) : '_');
    }
    SwString result(out);
    while (result.contains("__")) {
        result.replace("__", "_");
    }
    result = result.trimmed();
    if (result.isEmpty()) {
        result = "user";
    }
    return result;
}

static SwString normalizePhone_(SwString phone) {
    phone = phone.trimmed();
    const std::string in = phone.toStdString();
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            out.push_back(c);
            continue;
        }
        if (c == '+' && out.empty()) {
            out.push_back(c);
            continue;
        }
    }
    return SwString(out);
}

// FireBD is hardcoded for the standalone app (can still be overridden via CLI/env for dev).
static const SwString kWaHardcodedFireBdUrl = "https://whatsapp-fb909-default-rtdb.europe-west1.firebasedatabase.app";
static const SwString kWaHardcodedFireBdAuth = SwString(); // keep empty in test-mode rules
static constexpr int kWaHardcodedFireBdPollMs = 750;

static SwString fireBdUrl_() {
    const SwString env = envStr_("SW_FIREBD_URL").trimmed();
    if (!env.isEmpty()) {
        return env;
    }
    return kWaHardcodedFireBdUrl;
}

static SwString fireBdAuth_() {
    const SwString env = envStr_("SW_FIREBD_AUTH").trimmed();
    if (!env.isEmpty()) {
        return env;
    }
    return kWaHardcodedFireBdAuth;
}

static int fireBdPollMs_() {
    const SwString env = envStr_("SW_FIREBD_POLL_MS").trimmed();
    if (!env.isEmpty()) {
        bool ok = false;
        const int ms = env.toInt(&ok);
        if (ok && ms > 0) { return ms; }
    }
    return kWaHardcodedFireBdPollMs;
}

static SwChatBubbleRole bubbleRoleForString_(const SwString& role) {
    if (role == "out" || role == "user" || role == "me") {
        return SwChatBubbleRole::User;
    }
    return SwChatBubbleRole::Bot;
}

static bool isMediaKind_(const SwString& kind) {
    return kind == "image" || kind == "video";
}

static const SwString kWaChipCss = R"(
    SwButton {
        background-color: rgb(255, 255, 255);
        border-color: rgb(209, 215, 219);
        border-width: 1px;
        border-radius: 14px;
        color: rgb(84, 101, 111);
        font-size: 13px;
        padding: 4px 10px;
    }
    SwButton:hover { background-color: rgb(248, 249, 250); }
    SwButton:pressed { background-color: rgb(236, 238, 240); }
)";

static const SwString kWaChipSelectedCss = R"(
    SwButton {
        background-color: rgb(220, 252, 231);
        border-color: rgb(134, 239, 172);
        border-width: 1px;
        border-radius: 14px;
        color: rgb(22, 163, 74);
        font-size: 13px;
        padding: 4px 10px;
    }
    SwButton:hover { background-color: rgb(187, 247, 208); }
    SwButton:pressed { background-color: rgb(134, 239, 172); }
)";

WhatsAppWidget::WhatsAppWidget(SwWidget* parent, const SwString& storageRoot)
    : SwWidget(parent) {
    setStyleSheet("SwWidget { background-color: rgb(240, 242, 245); border-width: 0px; border-radius: 0px; }");
    setMinimumSize(640, 480);

    buildUi_();

    m_store = new WaLocalStore(storageRoot);
    if (m_store) {
        m_store->loadOrSeed();
        const WaLocalStore::UserProfile& user = m_store->user();
        if (m_navProfile) {
            if (!user.initial.isEmpty()) {
                m_navProfile->setInitial(user.initial);
            }
            m_navProfile->setColor(user.avatarColor);
        }
    }
    if (m_threadDelegate) {
        m_threadDelegate->setStore(m_store);
    }
    buildConversations_();
    buildThread_();
    updateLayout_();

    // Restore login session (SwSettings) or show auth page.
    restoreSessionOrShowLogin_();
}

WhatsAppWidget::~WhatsAppWidget() {
    if (m_fire) {
        m_fire->stop();
    }
    if (m_store) {
        if (m_msgEdit && !m_currentConversationId.isEmpty()) {
            m_store->setDraftForConversationId(m_currentConversationId, m_msgEdit->getText());
        }
        m_store->save();
    }
    delete m_store;
    m_store = nullptr;
}

void WhatsAppWidget::setLoggedIn_(bool on) {
    m_loggedIn = on;

    // Always close popups when switching modes.
    setEmojiPopupVisible_(false);
    setAttachPopupVisible_(false);

    if (!on && m_fire) {
        m_fire->stop();
    }

    if (m_nav) m_nav->setVisible(on);
    if (m_sepNav) m_sepNav->setVisible(on);
    if (m_left) m_left->setVisible(on);
    if (m_sepMid) m_sepMid->setVisible(on);
    if (m_right) m_right->setVisible(on);

    if (m_loginPage) {
        m_loginPage->setVisible(!on);
    }

    updateLayout_();

    if (on) {
        (void)ensureFireService_();
    }

    if (on && m_msgEdit) {
        m_msgEdit->setFocus(true);
    }
}

void WhatsAppWidget::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    updateLayout_();
}

void WhatsAppWidget::mousePressEvent(MouseEvent* event) {
    if (event) {
        auto contains = [](const SwRect& r, int px, int py) {
            return (px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height));
        };

        if (isEmojiPopupVisible_()) {
            const bool insidePopup = m_emojiPopup ? contains(m_emojiPopup->getRect(), event->x(), event->y()) : false;
            const bool insideEmojiBtn = m_emoji ? contains(m_emoji->getRect(), event->x(), event->y()) : false;
            if (!insidePopup && !insideEmojiBtn) {
                setEmojiPopupVisible_(false);
            }
        }

        if (isAttachPopupVisible_()) {
            const bool insidePopup = m_attachPopup ? contains(m_attachPopup->getRect(), event->x(), event->y()) : false;
            const bool insidePlusBtn = m_plus ? contains(m_plus->getRect(), event->x(), event->y()) : false;
            if (!insidePopup && !insidePlusBtn) {
                setAttachPopupVisible_(false);
            }
        }
    }
    SwWidget::mousePressEvent(event);
}

void WhatsAppWidget::keyPressEvent(KeyEvent* event) {
    if (event && SwWidgetPlatformAdapter::isEscapeKey(event->key())) {
        if (isEmojiPopupVisible_()) {
            setEmojiPopupVisible_(false);
            event->accept();
            return;
        }
        if (isAttachPopupVisible_()) {
            setAttachPopupVisible_(false);
            event->accept();
            return;
        }
    }
    SwWidget::keyPressEvent(event);
}

void WhatsAppWidget::buildUi_() {
    // Root panes.
    m_nav = new SwFrame(this);
    m_nav->setFrameShape(SwFrame::Shape::Box);
    m_nav->setStyleSheet("SwFrame { background-color: rgb(255, 255, 255); border-width: 0px; border-radius: 0px; }");

    m_sepNav = new SwFrame(this);
    m_sepNav->setFrameShape(SwFrame::Shape::Box);
    m_sepNav->setStyleSheet("SwFrame { background-color: rgb(209, 215, 219); border-width: 0px; border-radius: 0px; }");

    m_left = new SwFrame(this);
    m_left->setFrameShape(SwFrame::Shape::Box);
    m_left->setStyleSheet("SwFrame { background-color: rgb(255, 255, 255); border-width: 0px; border-radius: 0px; }");

    m_sepMid = new SwFrame(this);
    m_sepMid->setFrameShape(SwFrame::Shape::Box);
    m_sepMid->setStyleSheet("SwFrame { background-color: rgb(209, 215, 219); border-width: 0px; border-radius: 0px; }");

    m_right = new SwFrame(this);
    m_right->setFrameShape(SwFrame::Shape::Box);
    m_right->setStyleSheet("SwFrame { background-color: rgb(239, 234, 226); border-width: 0px; border-radius: 0px; }");

    // Nav rail.
    m_navChats = new WaIconButton(WaIconButton::Kind::Chats, m_nav);
    m_navChats->setActive(true);
    m_navChats->setBadgeCount(6);

    m_navCalls = new WaIconButton(WaIconButton::Kind::Phone, m_nav);
    m_navStatus = new WaIconButton(WaIconButton::Kind::Status, m_nav);

    m_navProfile = new WaAvatarCircle("M", SwColor{82, 196, 26}, m_nav);
    m_navSettings = new WaIconButton(WaIconButton::Kind::Settings, m_nav);
    SwObject::connect(m_navSettings, &WaIconButton::clicked, [this]() { logout_(); });

    // Left top (title + search + chips).
    m_leftTop = new SwFrame(m_left);
    m_leftTop->setFrameShape(SwFrame::Shape::Box);
    m_leftTop->setStyleSheet("SwFrame { background-color: rgb(240, 242, 245); border-width: 0px; border-radius: 0px; }");

    m_title = new SwLabel("Discussions", m_leftTop);
    m_title->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(17, 27, 33); font-size: 18px; }");

    m_newChat = new WaIconButton(WaIconButton::Kind::NewChat, m_leftTop);
    m_menu = new WaIconButton(WaIconButton::Kind::Menu, m_leftTop);
    SwObject::connect(m_newChat, &WaIconButton::clicked, [this]() { openNewChat_(); });
    SwObject::connect(m_menu, &WaIconButton::clicked, [this]() { logout_(); });

    m_search = new SwLineEdit("Rechercher ou démarrer une discussion", m_leftTop);
    m_search->setStyleSheet(R"(
        SwLineEdit {
            background-color: rgb(255, 255, 255);
            border-width: 0px;
            border-radius: 18px;
            padding: 6px 12px;
            padding-left: 38px;
            color: rgb(17, 27, 33);
        }
    )");

    m_searchIcon = new WaIconButton(WaIconButton::Kind::Search, m_leftTop);

    m_chipAll = new SwButton("Toutes", m_leftTop);
    m_chipAll->setStyleSheet(kWaChipSelectedCss);
    m_chipUnread = new SwButton("Non lues", m_leftTop);
    m_chipUnread->setStyleSheet(kWaChipCss);
    m_chipFav = new SwButton("Favoris", m_leftTop);
    m_chipFav->setStyleSheet(kWaChipCss);
    m_chipGroups = new SwButton("Groupes", m_leftTop);
    m_chipGroups->setStyleSheet(kWaChipCss);

    SwObject::connect(m_chipAll, &SwButton::clicked, [this]() { setConversationFilter_(ConversationFilter::All); });
    SwObject::connect(m_chipUnread, &SwButton::clicked, [this]() { setConversationFilter_(ConversationFilter::Unread); });
    SwObject::connect(m_chipFav, &SwButton::clicked, [this]() { setConversationFilter_(ConversationFilter::Favorites); });
    SwObject::connect(m_chipGroups, &SwButton::clicked, [this]() { setConversationFilter_(ConversationFilter::Groups); });

    // Conversation list.
    m_convoList = new SwListWidget(m_left);
    m_convoList->setRowHeight(72);
    m_convoList->setAlternatingRowColors(false);
    m_convoDelegate = new WaConversationListDelegate(m_convoList);
    m_convoList->setItemDelegate(m_convoDelegate);
    m_convoList->setViewportPadding(0);
    m_convoList->setScrollBarThickness(8);
    m_convoList->setStyleSheet("SwListWidget { background-color: rgb(255, 255, 255); border-width: 0px; border-radius: 0px; }");

    if (m_convoList && m_convoList->selectionModel()) {
        SwObject::connect(m_convoList->selectionModel(),
                          &SwItemSelectionModel::currentChanged,
                          [this](const SwModelIndex& current, const SwModelIndex&) {
                              if (!current.isValid()) {
                                  return;
                              }
                              const int row = current.row();
                              if (row < 0 || row >= m_conversationIds.size()) {
                                  return;
                              }
                              setCurrentConversationId_(m_conversationIds[row]);
                          });
    }

    // Right header.
    m_header = new SwFrame(m_right);
    m_header->setFrameShape(SwFrame::Shape::Box);
    m_header->setStyleSheet("SwFrame { background-color: rgb(240, 242, 245); border-width: 0px; border-radius: 0px; }");

    m_headerAvatar = new WaAvatarCircle("M", SwColor{82, 196, 26}, m_header);

    m_headerName = new SwLabel("Maxence Merio", m_header);
    m_headerName->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(17, 27, 33); font-size: 15px; }");

    m_headerStatus = new SwLabel("en ligne", m_header);
    m_headerStatus->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(102, 119, 129); font-size: 12px; }");

    m_btnVideo = new WaIconButton(WaIconButton::Kind::Video, m_header);
    m_btnPhone = new WaIconButton(WaIconButton::Kind::Phone, m_header);
    m_btnSearch = new WaIconButton(WaIconButton::Kind::Search, m_header);
    m_btnMore = new WaIconButton(WaIconButton::Kind::Menu, m_header);

    // Chat background + thread view.
    m_chatBg = new WaChatWallpaper(m_right);

    m_threadView = new SwListView(m_chatBg);
    m_threadView->setRowHeight(40);
    m_threadView->setUniformRowHeights(false);
    m_threadView->setAlternatingRowColors(false);
    m_threadView->setViewportPadding(0);
    m_threadView->setScrollBarThickness(8);
    m_threadView->setStyleSheet("SwListView { background-color: rgba(0,0,0,0); border-width: 0px; border-radius: 0px; }");

    m_threadDelegate = new WaThreadBubbleDelegate(m_threadView);
    m_threadDelegate->setDefaultImage(WaDemoAssets::makeFakeScreenshotThumb(420, 236, 16));
    m_threadDelegate->setDefaultVideo(WaDemoAssets::makeVideoThumb(420, 236, 16));
    m_threadView->setItemDelegate(m_threadDelegate);

    SwObject::connect(m_threadView, &SwListView::doubleClicked, [this](const SwModelIndex& idx) {
        if (!idx.isValid() || !m_threadModel || !m_store) {
            return;
        }
        const int r = idx.row();
        if (r < 0 || r >= m_threadModel->rowCount()) {
            return;
        }

        const SwString kind = m_threadModel->data(m_threadModel->index(r, 2), SwItemDataRole::DisplayRole).toString();
        const SwString payload = m_threadModel->data(m_threadModel->index(r, 5), SwItemDataRole::DisplayRole).toString();
        if (payload.isEmpty()) {
            return;
        }

        const SwString abs = m_store->resolveMediaPath(payload);
        if (kind == "image") {
            auto* dlg = new WaMediaViewerDialog(WaMediaViewerDialog::Kind::Image, abs, this);
            SwObject::connect(dlg, &SwDialog::finished, [dlg](int) { delete dlg; });
            dlg->open();
        } else if (kind == "video") {
            auto* dlg = new WaMediaViewerDialog(WaMediaViewerDialog::Kind::Video, abs, this);
            SwObject::connect(dlg, &SwDialog::finished, [dlg](int) { delete dlg; });
            dlg->open();
        }
    });

    // Input bar.
    m_input = new SwFrame(m_right);
    m_input->setFrameShape(SwFrame::Shape::Box);
    m_input->setStyleSheet("SwFrame { background-color: rgb(240, 242, 245); border-width: 0px; border-radius: 0px; }");

    m_plus = new WaIconButton(WaIconButton::Kind::Plus, m_input);
    m_emoji = new WaIconButton(WaIconButton::Kind::Emoji, m_input);

    m_msgEdit = new WaMessageEdit("Entrez un message", m_input);
    m_msgEdit->setMaxLines(5);
    m_msgEdit->setWordWrapEnabled(true);
    m_msgEdit->setStyleSheet(R"(
        WaMessageEdit {
            background-color: rgb(255, 255, 255);
            border-width: 0px;
            border-radius: 18px;
            padding: 6px 26px 6px 12px;
            color: rgb(17, 27, 33);
        }
    )");

    m_mic = new WaIconButton(WaIconButton::Kind::Mic, m_input);

    // Emoji popup (overlay) - kept as last child so it paints above other panes.
    m_emojiPopup = new WaEmojiPickerPopup(this);
    m_emojiPopup->hide();

    // Attach (+) popup (overlay).
    m_attachPopup = new WaAttachMenuPopup(this);
    m_attachPopup->hide();

    SwObject::connect(m_plus, &WaIconButton::clicked, [this]() { toggleAttachPopup_(); });
    SwObject::connect(m_emoji, &WaIconButton::clicked, [this]() { toggleEmojiPopup_(); });
    SwObject::connect(m_emojiPopup, &WaEmojiPickerPopup::emojiPicked, [this](const SwString& emoji) {
        if (!m_msgEdit) {
            return;
        }
        m_msgEdit->setText(m_msgEdit->getText() + emoji);
        m_msgEdit->setFocus(true);
    });

    SwObject::connect(m_msgEdit, &WaMessageEdit::submitted, [this]() { sendMessage_(); });
    SwObject::connect(m_msgEdit, &SwPlainTextEdit::textChanged, [this]() {
        updateInputActions_();
        updateLayout_();
    });
    SwObject::connect(m_mic, &WaIconButton::clicked, [this]() { sendMessage_(); });

    SwObject::connect(m_attachPopup, &WaAttachMenuPopup::actionTriggered, [this](const SwString& action) {
        setAttachPopupVisible_(false);

        if (!m_store || m_currentConversationId.isEmpty()) {
            if (m_msgEdit) {
                m_msgEdit->setFocus(true);
            }
            return;
        }

        if (action == "media") {
            const SwString file = SwFileDialog::getOpenFileName(
                this,
                "Choisir un média",
                SwString(),
                "Images (*.png *.jpg *.jpeg *.bmp);;Vidéos (*.mp4 *.mov *.avi *.mkv);;Tous (*.*)");

            if (!file.isEmpty()) {
                SwFileInfo info(file.toStdString());
                SwString ext = SwString(info.suffix()).toLower();
                SwString kind;
                if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp") {
                    kind = "image";
                } else if (ext == "mp4" || ext == "mov" || ext == "avi" || ext == "mkv") {
                    kind = "video";
                }

                if (!kind.isEmpty()) {
                    SwString payload = m_store->importMediaFile(file);
                    if (payload.isEmpty()) {
                        payload = file;
                    }
                    appendOutgoingMessage_(SwString(), kind, payload);
                }
            }
        } else if (action == "file") {
            const SwString file = SwFileDialog::getOpenFileName(this, "Choisir un fichier", SwString(), "Tous (*.*)");
            if (!file.isEmpty()) {
                SwFileInfo info(file.toStdString());
                const SwString text = SwString(info.fileName());
                SwString payload = m_store->importMediaFile(file);
                if (payload.isEmpty()) {
                    payload = file;
                }
                appendOutgoingMessage_(text, "file", payload);
            }
        }

        if (m_msgEdit) {
            m_msgEdit->setFocus(true);
        }
    });

    // Login overlay (covers the whole UI until authenticated).
    m_loginPage = new WaLoginPage(this);
    if (m_loginPage) {
        m_loginPage->setDatabaseUrl(fireBdUrl_());
        m_loginPage->setAuthToken(fireBdAuth_());
        const int pollMs = fireBdPollMs_();
        if (pollMs > 0) {
            m_loginPage->setPollIntervalMs(pollMs);
        }

        SwObject::connect(m_loginPage, &WaLoginPage::loginRequested, [this](const SwString& idOrPhone, const SwString& password) {
            onAuthLoginRequested_(idOrPhone, password);
        });
        SwObject::connect(m_loginPage,
                          &WaLoginPage::signUpRequested,
                          [this](const SwString& firstName, const SwString& lastName, const SwString& pseudo, const SwString& phone, const SwString& password) {
                              onAuthSignUpRequested_(firstName, lastName, pseudo, phone, password);
                          });
    }

    updateInputActions_();
}

void WhatsAppWidget::buildConversations_(bool allowFallbackSelection) {
    if (!m_convoList || !m_store) {
        return;
    }

    if (m_convoList) {
        m_convoList->clear();
    }
    m_conversationIds.clear();
    m_conversationRows.clear();

    struct RowData {
        SwString initial;
        SwColor color;
        SwString title;
        SwString preview;
        SwString timeText;
        int unread{0};
    };

    SwList<RowData> rows;
    const SwList<WaLocalStore::Conversation>& convos = m_store->conversations();
    rows.reserve(convos.size());
    for (int i = 0; i < convos.size(); ++i) {
        const WaLocalStore::Conversation& c = convos[i];

        bool include = true;
        switch (m_conversationFilter) {
        case ConversationFilter::Unread:
            include = c.unreadCount > 0;
            break;
        case ConversationFilter::Favorites:
            include = c.favorite;
            break;
        case ConversationFilter::Groups:
            include = c.type == "group";
            break;
        default:
            include = true;
            break;
        }
        if (!include) {
            continue;
        }

        const SwString title = m_store->titleForConversationId(c.id);
        m_convoList->addItem(title);
        m_conversationIds.append(c.id);

        RowData row;
        row.initial = m_store->initialForConversationId(c.id);
        row.color = m_store->avatarColorForConversationId(c.id);
        row.title = title;
        row.preview = c.lastPreviewText();
        row.timeText = c.lastTimeText();
        row.unread = c.unreadCount;
        rows.append(row);
    }

    if (m_convoList->model()) {
        for (int row = 0; row < rows.size(); ++row) {
            const RowData& d = rows[static_cast<size_t>(row)];
            const SwModelIndex idx = m_convoList->model()->index(row, 0);
            auto* rowWidget = new WaConversationRowWidget(d.initial, d.color, d.title, d.preview, d.timeText, d.unread, m_convoList);
            m_conversationRows.append(rowWidget);
            m_convoList->setIndexWidget(idx, rowWidget);
        }
    }

    if (m_convoList && m_convoList->selectionModel() && m_convoList->model()) {
        const SwString selectedId = m_store->selectedConversationId();
        const int row = rowForConversationId_(selectedId);
        if (row >= 0 && row < m_convoList->model()->rowCount()) {
            m_convoList->selectionModel()->setCurrentIndex(m_convoList->model()->index(row, 0));
        } else if (allowFallbackSelection && m_convoList->model()->rowCount() > 0) {
            m_convoList->selectionModel()->setCurrentIndex(m_convoList->model()->index(0, 0));
        } else {
            m_convoList->selectionModel()->clear();
        }
    }
}

void WhatsAppWidget::setConversationFilter_(ConversationFilter filter) {
    if (m_conversationFilter == filter) {
        return;
    }
    m_conversationFilter = filter;
    updateConversationFilterUi_();
    buildConversations_(false);
}

void WhatsAppWidget::updateConversationFilterUi_() {
    if (m_chipAll) {
        m_chipAll->setStyleSheet(m_conversationFilter == ConversationFilter::All ? kWaChipSelectedCss : kWaChipCss);
    }
    if (m_chipUnread) {
        m_chipUnread->setStyleSheet(m_conversationFilter == ConversationFilter::Unread ? kWaChipSelectedCss : kWaChipCss);
    }
    if (m_chipFav) {
        m_chipFav->setStyleSheet(m_conversationFilter == ConversationFilter::Favorites ? kWaChipSelectedCss : kWaChipCss);
    }
    if (m_chipGroups) {
        m_chipGroups->setStyleSheet(m_conversationFilter == ConversationFilter::Groups ? kWaChipSelectedCss : kWaChipCss);
    }
}

void WhatsAppWidget::buildThread_() {
    if (!m_threadView) {
        return;
    }
    if (!m_store) {
        return;
    }

    if (m_currentConversationId.isEmpty()) {
        m_currentConversationId = m_store->selectedConversationId();
    }

    const WaLocalStore::Conversation* convo = m_store->conversationById(m_currentConversationId);
    if (!convo) {
        return;
    }

    m_lastThreadTextBubble = nullptr;

    SwStandardItemModel* oldModel = m_threadModel;
    m_threadModel = new SwStandardItemModel(0, 7, m_threadView);
    for (int i = 0; i < convo->messages.size(); ++i) {
        const WaLocalStore::Message& m = convo->messages[i];
        SwList<SwStandardItem*> row;
        row.append(new SwStandardItem(m.text));      // text
        row.append(new SwStandardItem(m.role));      // role
        row.append(new SwStandardItem(m.kind));      // kind
        row.append(new SwStandardItem(m.meta));      // meta
        row.append(new SwStandardItem(m.reaction));  // reaction
        row.append(new SwStandardItem(m.payload));   // payload
        row.append(new SwStandardItem(m.status));    // status
        m_threadModel->appendRow(row);
    }

    m_threadView->setModel(m_threadModel);
    if (oldModel) {
        delete oldModel;
    }

    // Make text bubbles selectable (read-only) via index widgets layered over the delegate paint.
    const SwChatBubbleTheme theme = m_threadDelegate ? m_threadDelegate->theme() : SwChatBubble::defaultTheme();
    for (int r = 0; r < m_threadModel->rowCount(); ++r) {
        const SwString kind = m_threadModel->data(m_threadModel->index(r, 2), SwItemDataRole::DisplayRole).toString();
        if (isMediaKind_(kind)) {
            continue;
        }

        SwChatBubbleMessage msg;
        msg.text = m_threadModel->data(m_threadModel->index(r, 0), SwItemDataRole::DisplayRole).toString();
        msg.meta = m_threadModel->data(m_threadModel->index(r, 3), SwItemDataRole::DisplayRole).toString();
        msg.reaction = m_threadModel->data(m_threadModel->index(r, 4), SwItemDataRole::DisplayRole).toString();
        msg.role = bubbleRoleForString_(m_threadModel->data(m_threadModel->index(r, 1), SwItemDataRole::DisplayRole).toString());
        msg.kind = SwChatMessageKind::Text;
        {
            const SwString status = m_threadModel->data(m_threadModel->index(r, 6), SwItemDataRole::DisplayRole).toString().toLower();
            if (status == "sent") {
                msg.status = SwChatMessageStatus::Sent;
            } else if (status == "delivered") {
                msg.status = SwChatMessageStatus::Delivered;
            } else if (status == "read") {
                msg.status = SwChatMessageStatus::Read;
            } else {
                msg.status = SwChatMessageStatus::None;
            }
        }

        auto* bubble = new SwChatBubble(m_threadView);
        bubble->setTheme(theme);
        bubble->setMessage(msg);
        bubble->setTextSelectable(true);
        bubble->setProperty("FillCell", true);

        m_lastThreadTextBubble = bubble;
        m_threadView->setIndexWidget(m_threadModel->index(r, 0), bubble);
    }
    m_threadView->scrollToBottom();
}

void WhatsAppWidget::updateEmojiPopupLayout_() {
    if (!m_emojiPopup || !m_right || !m_input) {
        return;
    }
    if (!m_emojiPopup->getVisible()) {
        return;
    }

    const SwRect rightR = m_right->getRect();
    const SwRect inputR = m_input->getRect();
    const SwRect chatR = m_chatBg ? m_chatBg->getRect() : rightR;

    const int pad = 10;
    const int gap = 10;

    int popupW = 360;
    int popupH = 320;

    const int availW = std::max(0, rightR.width - 2 * pad);
    popupW = std::min(popupW, availW);
    if (popupW < 240) {
        popupW = availW;
    }

    const int minY = chatR.y + gap;
    const int maxH = inputR.y - gap - minY;
    if (maxH <= 0 || popupW <= 0) {
        setEmojiPopupVisible_(false);
        return;
    }
    popupH = std::min(popupH, maxH);
    if (popupH < 180) {
        popupH = maxH;
    }
    if (popupH <= 0) {
        setEmojiPopupVisible_(false);
        return;
    }

    int x = inputR.x + pad;
    const int minX = rightR.x + gap;
    const int maxX = rightR.x + rightR.width - gap - popupW;
    if (maxX >= minX) {
        x = clampInt_(x, minX, maxX);
    } else {
        x = minX;
    }

    int y = inputR.y - gap - popupH;
    if (y < minY) {
        y = minY;
    }

    m_emojiPopup->move(x, y);
    m_emojiPopup->resize(popupW, popupH);
}

void WhatsAppWidget::setEmojiPopupVisible_(bool on) {
    if (!m_emojiPopup) {
        return;
    }

    if (on == m_emojiPopup->getVisible()) {
        return;
    }

    if (on) {
        setAttachPopupVisible_(false);
        m_emojiPopup->show();
        updateEmojiPopupLayout_();
    } else {
        m_emojiPopup->hide();
    }

    if (m_emoji) {
        m_emoji->setActive(on);
    }
}

bool WhatsAppWidget::isEmojiPopupVisible_() const {
    return m_emojiPopup && m_emojiPopup->getVisible();
}

void WhatsAppWidget::toggleEmojiPopup_() {
    setEmojiPopupVisible_(!isEmojiPopupVisible_());
}

void WhatsAppWidget::updateAttachPopupLayout_() {
    if (!m_attachPopup || !m_right || !m_input || !m_plus) {
        return;
    }
    if (!m_attachPopup->getVisible()) {
        return;
    }

    const SwRect rightR = m_right->getRect();
    const SwRect inputR = m_input->getRect();
    const SwRect chatR = m_chatBg ? m_chatBg->getRect() : rightR;
    const SwRect plusR = m_plus->getRect();

    const int pad = 10;
    const int gap = 10;

    int popupW = 260;
    int popupH = 224;

    const int availW = std::max(0, rightR.width - 2 * pad);
    popupW = std::min(popupW, availW);
    if (popupW < 200) {
        popupW = availW;
    }

    const int minY = chatR.y + gap;
    const int maxH = inputR.y - gap - minY;
    if (maxH <= 0 || popupW <= 0) {
        setAttachPopupVisible_(false);
        return;
    }
    popupH = std::min(popupH, maxH);
    if (popupH <= 0) {
        setAttachPopupVisible_(false);
        return;
    }

    int x = plusR.x;
    const int minX = rightR.x + gap;
    const int maxX = rightR.x + rightR.width - gap - popupW;
    if (maxX >= minX) {
        x = clampInt_(x, minX, maxX);
    } else {
        x = minX;
    }

    int y = inputR.y - gap - popupH;
    if (y < minY) {
        y = minY;
    }

    m_attachPopup->move(x, y);
    m_attachPopup->resize(popupW, popupH);
}

void WhatsAppWidget::setAttachPopupVisible_(bool on) {
    if (!m_attachPopup) {
        return;
    }
    if (on == m_attachPopup->getVisible()) {
        return;
    }

    if (on) {
        setEmojiPopupVisible_(false);
        m_attachPopup->show();
        updateAttachPopupLayout_();
    } else {
        m_attachPopup->hide();
    }

    if (m_plus) {
        m_plus->setActive(on);
    }
}

bool WhatsAppWidget::isAttachPopupVisible_() const {
    return m_attachPopup && m_attachPopup->getVisible();
}

void WhatsAppWidget::toggleAttachPopup_() {
    setAttachPopupVisible_(!isAttachPopupVisible_());
}

void WhatsAppWidget::setComposerTextForSnapshot(const SwString& text) {
    if (!m_msgEdit) {
        return;
    }
    m_msgEdit->setText(text);
    updateInputActions_();
    updateLayout_();
}

void WhatsAppWidget::appendTextMessageForSnapshot(const SwString& text) {
    if (!m_store) {
        return;
    }
    if (text.isEmpty()) {
        return;
    }

    if (m_currentConversationId.isEmpty()) {
        m_currentConversationId = m_store->selectedConversationId();
    }
    if (m_currentConversationId.isEmpty() && !m_store->conversations().isEmpty()) {
        m_currentConversationId = m_store->conversations()[0].id;
    }
    if (m_currentConversationId.isEmpty()) {
        return;
    }

    const SwString meta = "15:28";
    m_store->appendMessage(m_currentConversationId, "out", text, meta, SwString(), SwString());
    m_store->markRead(m_currentConversationId);
    m_store->save();

    buildThread_();
    refreshConversationRowUi_(m_currentConversationId);
    if (m_threadView) {
        m_threadView->scrollToBottom();
    }
    update();
}

void WhatsAppWidget::appendMediaMessageForSnapshot(const SwString& kind, const SwString& payload) {
    if (!m_store) {
        return;
    }
    if (kind.isEmpty() || payload.isEmpty()) {
        return;
    }

    if (m_currentConversationId.isEmpty()) {
        m_currentConversationId = m_store->selectedConversationId();
    }
    if (m_currentConversationId.isEmpty() && !m_store->conversations().isEmpty()) {
        m_currentConversationId = m_store->conversations()[0].id;
    }
    if (m_currentConversationId.isEmpty()) {
        return;
    }

    const SwString meta = (kind == "video") ? "15:27" : "15:26";
    m_store->appendMessage(m_currentConversationId, "out", SwString(), meta, kind, payload);
    m_store->markRead(m_currentConversationId);
    m_store->save();

    buildThread_();
    refreshConversationRowUi_(m_currentConversationId);
    if (m_threadView) {
        m_threadView->scrollToBottom();
    }
    update();
}

void WhatsAppWidget::setLastTextSelectionForSnapshot(int start, int end) {
    if (!m_lastThreadTextBubble) {
        return;
    }

    const int s = std::max(0, start);
    const int e = std::max(0, end);
    m_lastThreadTextBubble->setTextSelectable(true);
    m_lastThreadTextBubble->setSelectionRange(static_cast<size_t>(s), static_cast<size_t>(e));
    update();
}

void WhatsAppWidget::updateInputActions_() {
    if (!m_msgEdit || !m_mic) {
        return;
    }
    const SwString text = m_msgEdit->getText().trimmed();
    m_mic->setKind(text.isEmpty() ? WaIconButton::Kind::Mic : WaIconButton::Kind::Send);
}

void WhatsAppWidget::refreshConversationRowUi_(const SwString& conversationId) {
    if (!m_store) {
        return;
    }
    const int convoRow = rowForConversationId_(conversationId);
    WaLocalStore::Conversation* c = m_store->conversationById(conversationId);
    if (c && convoRow >= 0 && convoRow < m_conversationRows.size() && m_conversationRows[convoRow]) {
        m_conversationRows[convoRow]->setPreviewText(c->lastPreviewText());
        m_conversationRows[convoRow]->setTimeText(c->lastTimeText());
        m_conversationRows[convoRow]->setUnreadCount(c->unreadCount);
    }
}

void WhatsAppWidget::appendOutgoingMessage_(const SwString& text, const SwString& kind, const SwString& payload) {
    if (!m_threadModel || !m_store) {
        return;
    }
    if (m_currentConversationId.isEmpty()) {
        return;
    }

    const SwString meta = nowHm_();
    SwString messageId;
    SwString status;
    if (m_loggedIn && ensureFireService_() && m_fire) {
        SwString sendKind = kind;
        if (sendKind.isEmpty()) {
            sendKind = "text";
        }
        messageId = m_fire->sendMessage(m_currentConversationId, m_currentConversationId, text, sendKind, payload, meta);
        if (!messageId.isEmpty()) {
            status = "sent";
        }
    }

    m_store->appendMessage(m_currentConversationId,
                           "out",
                           text,
                           meta,
                           kind,
                           payload,
                           SwString(),
                           messageId,
                           fireUserId_(),
                           status);
    m_store->markRead(m_currentConversationId);
    m_store->setSelectedConversationId(m_currentConversationId);
    m_store->save();

    SwList<SwStandardItem*> row;
    row.append(new SwStandardItem(text));     // text
    row.append(new SwStandardItem("out"));    // role
    row.append(new SwStandardItem(kind));     // kind
    row.append(new SwStandardItem(meta));     // meta
    row.append(new SwStandardItem(""));       // reaction
    row.append(new SwStandardItem(payload));  // payload
    row.append(new SwStandardItem(status));   // status
    m_threadModel->appendRow(row);

    if (m_threadView && !isMediaKind_(kind)) {
        const int r = m_threadModel->rowCount() - 1;
        if (r >= 0) {
            SwChatBubbleMessage msg;
            msg.text = text;
            msg.meta = meta;
            msg.reaction = SwString();
            msg.role = SwChatBubbleRole::User;
            msg.kind = SwChatMessageKind::Text;
            if (status == "sent") {
                msg.status = SwChatMessageStatus::Sent;
            } else if (status == "delivered") {
                msg.status = SwChatMessageStatus::Delivered;
            } else if (status == "read") {
                msg.status = SwChatMessageStatus::Read;
            }

            auto* bubble = new SwChatBubble(m_threadView);
            bubble->setTheme(m_threadDelegate ? m_threadDelegate->theme() : SwChatBubble::defaultTheme());
            bubble->setMessage(msg);
            bubble->setTextSelectable(true);
            bubble->setProperty("FillCell", true);

            m_lastThreadTextBubble = bubble;
            m_threadView->setIndexWidget(m_threadModel->index(r, 0), bubble);
        }
    }

    if (m_threadView) {
        m_threadView->scrollToBottom();
    }
    refreshConversationRowUi_(m_currentConversationId);
}

void WhatsAppWidget::sendMessage_() {
    if (!m_msgEdit || !m_mic || !m_threadModel || !m_store) {
        return;
    }

    const SwString text = m_msgEdit->getText().trimmed();
    if (text.isEmpty()) {
        return;
    }

    if (m_currentConversationId.isEmpty()) {
        return;
    }

    m_store->setDraftForConversationId(m_currentConversationId, SwString());
    appendOutgoingMessage_(text, SwString(), SwString());

    m_msgEdit->setText("");
    m_msgEdit->setFocus(true);
    updateInputActions_();

    refreshConversationRowUi_(m_currentConversationId);
}

void WhatsAppWidget::setCurrentConversationId_(const SwString& conversationId) {
    if (conversationId.isEmpty() || !m_store) {
        return;
    }
    if (m_currentConversationId == conversationId) {
        return;
    }

    if (m_msgEdit && !m_currentConversationId.isEmpty()) {
        m_store->setDraftForConversationId(m_currentConversationId, m_msgEdit->getText());
    }

    m_currentConversationId = conversationId;
    m_store->setSelectedConversationId(conversationId);
    m_store->markRead(conversationId);
    sendReadAcksForConversation_(conversationId);
    m_store->save();

    if (m_headerAvatar) {
        m_headerAvatar->setInitial(m_store->initialForConversationId(conversationId));
        m_headerAvatar->setColor(m_store->avatarColorForConversationId(conversationId));
    }
    if (m_headerName) {
        m_headerName->setText(m_store->titleForConversationId(conversationId));
    }
    if (m_headerStatus) {
        m_headerStatus->setText(m_store->statusForConversationId(conversationId));
    }

    const int row = rowForConversationId_(conversationId);
    const WaLocalStore::Conversation* c = m_store->conversationById(conversationId);
    if (c && row >= 0 && row < m_conversationRows.size() && m_conversationRows[row]) {
        m_conversationRows[row]->setPreviewText(c->lastPreviewText());
        m_conversationRows[row]->setTimeText(c->lastTimeText());
        m_conversationRows[row]->setUnreadCount(0);
    }

    buildThread_();

    if (m_msgEdit) {
        m_msgEdit->setText(m_store->draftForConversationId(conversationId));
        updateInputActions_();
        if (m_loggedIn) {
            m_msgEdit->setFocus(true);
        }
    }

    if (m_conversationFilter == ConversationFilter::Unread) {
        buildConversations_(false);
    }
}

int WhatsAppWidget::rowForConversationId_(const SwString& conversationId) const {
    if (conversationId.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < m_conversationIds.size(); ++i) {
        if (m_conversationIds[i] == conversationId) {
            return i;
        }
    }
    return -1;
}

void WhatsAppWidget::updateLayout_() {
    const SwRect r = getRect();
    const int w = r.width;
    const int h = r.height;

    const int baseX = r.x;
    const int baseY = r.y;

    const int navW = 64;
    const int sepW = 1;
    const int minRightW = 360;
    const int minLeftW = 280;

    int leftW = 420;
    int maxLeftW = w - navW - sepW * 2 - minRightW;
    if (maxLeftW < minLeftW) {
        maxLeftW = minLeftW;
    }
    leftW = clampInt_(leftW, minLeftW, maxLeftW);

    const int leftX = baseX + navW + sepW;
    const int rightX = baseX + navW + sepW + leftW + sepW;
    const int rightW = w - (rightX - baseX);

    // Root panes.
    if (m_nav) {
        m_nav->move(baseX, baseY);
        m_nav->resize(navW, h);
    }
    if (m_sepNav) {
        m_sepNav->move(baseX + navW, baseY);
        m_sepNav->resize(sepW, h);
    }
    if (m_left) {
        m_left->move(leftX, baseY);
        m_left->resize(leftW, h);
    }
    if (m_sepMid) {
        m_sepMid->move(leftX + leftW, baseY);
        m_sepMid->resize(sepW, h);
    }
    if (m_right) {
        m_right->move(rightX, baseY);
        m_right->resize(rightW, h);
    }

    // Nav rail.
    if (m_nav) {
        const SwRect navR = m_nav->getRect();
        if (m_navChats) { m_navChats->move(navR.x + 12, navR.y + 12); m_navChats->resize(40, 40); }
        if (m_navCalls) { m_navCalls->move(navR.x + 12, navR.y + 60); m_navCalls->resize(40, 40); }
        if (m_navStatus) { m_navStatus->move(navR.x + 12, navR.y + 108); m_navStatus->resize(40, 40); }
        if (m_navProfile) { m_navProfile->move(navR.x + 12, navR.y + h - 100); m_navProfile->resize(40, 40); }
        if (m_navSettings) { m_navSettings->move(navR.x + 12, navR.y + h - 52); m_navSettings->resize(40, 40); }
    }

    // Left: top area + conversation list.
    const int leftTopH = 152;
    if (m_leftTop && m_left) {
        const SwRect leftR = m_left->getRect();
        m_leftTop->move(leftR.x, leftR.y);
        m_leftTop->resize(leftW, leftTopH);

        const SwRect topR = m_leftTop->getRect();

        if (m_title) {
            m_title->move(topR.x + 16, topR.y + 12);
            m_title->resize(leftW - 120, 28);
        }

        if (m_newChat) { m_newChat->move(topR.x + leftW - 88, topR.y + 10); m_newChat->resize(36, 36); }
        if (m_menu) { m_menu->move(topR.x + leftW - 48, topR.y + 10); m_menu->resize(36, 36); }

        if (m_search) {
            m_search->move(topR.x + 12, topR.y + 56);
            m_search->resize(leftW - 24, 36);
        }
        if (m_searchIcon) {
            m_searchIcon->move(topR.x + 18, topR.y + 58);
            m_searchIcon->resize(32, 32);
        }

        int chipY = topR.y + 104;
        int chipX = topR.x + 12;

        if (m_chipAll) { m_chipAll->move(chipX, chipY); m_chipAll->resize(76, 28); chipX += 76 + 8; }
        if (m_chipUnread) { m_chipUnread->move(chipX, chipY); m_chipUnread->resize(86, 28); chipX += 86 + 8; }
        if (m_chipFav) { m_chipFav->move(chipX, chipY); m_chipFav->resize(78, 28); chipX += 78 + 8; }
        if (m_chipGroups) { m_chipGroups->move(chipX, chipY); m_chipGroups->resize(82, 28); }
    }

    if (m_convoList && m_left) {
        const SwRect leftR = m_left->getRect();
        m_convoList->move(leftR.x, leftR.y + leftTopH);
        m_convoList->resize(leftW, h - leftTopH);
    }

    // Right: header + chat + input.
    const int headerH = 60;
    const int inputPadY = 14;

    // Ensure the composer width is up-to-date before computing its preferred height
    // (word-wrapping depends on the available width).
    const int composerX = 98;
    const int composerRightPad = 54;
    const int composerW = std::max(0, rightW - composerX - composerRightPad);
    if (m_msgEdit && composerW > 0) {
        const SwRect mr = m_msgEdit->getRect();
        if (mr.width != composerW) {
            m_msgEdit->resize(composerW, mr.height);
        }
    }

    const int composerH = m_msgEdit ? m_msgEdit->preferredHeight() : 36;
    const int inputH = std::max(64, composerH + 2 * inputPadY);
    const int chatH = std::max(0, h - headerH - inputH);

    if (m_header && m_right) {
        const SwRect rightR = m_right->getRect();
        m_header->move(rightR.x, rightR.y);
        m_header->resize(rightW, headerH);

        const SwRect headerR = m_header->getRect();
        if (m_headerAvatar) { m_headerAvatar->move(headerR.x + 16, headerR.y + 10); m_headerAvatar->resize(40, 40); }
        if (m_headerName) { m_headerName->move(headerR.x + 64, headerR.y + 10); m_headerName->resize(320, 22); }
        if (m_headerStatus) { m_headerStatus->move(headerR.x + 64, headerR.y + 30); m_headerStatus->resize(320, 18); }

        if (m_btnVideo) { m_btnVideo->move(headerR.x + rightW - 172, headerR.y + 12); m_btnVideo->resize(36, 36); }
        if (m_btnPhone) { m_btnPhone->move(headerR.x + rightW - 132, headerR.y + 12); m_btnPhone->resize(36, 36); }
        if (m_btnSearch) { m_btnSearch->move(headerR.x + rightW - 92, headerR.y + 12); m_btnSearch->resize(36, 36); }
        if (m_btnMore) { m_btnMore->move(headerR.x + rightW - 52, headerR.y + 12); m_btnMore->resize(36, 36); }
    }

    if (m_chatBg && m_right) {
        const SwRect rightR = m_right->getRect();
        m_chatBg->move(rightR.x, rightR.y + headerH);
        m_chatBg->resize(rightW, chatH);
    }

    if (m_threadView && m_chatBg) {
        const SwRect chatR = m_chatBg->getRect();
        m_threadView->move(chatR.x, chatR.y);
        m_threadView->resize(rightW, chatH);
    }

    if (m_input && m_right) {
        const SwRect rightR = m_right->getRect();
        m_input->move(rightR.x, rightR.y + h - inputH);
        m_input->resize(rightW, inputH);

        const SwRect inputR = m_input->getRect();
        const int iconY = inputR.y + inputR.height - inputPadY - 36;
        if (m_plus) { m_plus->move(inputR.x + 16, iconY); m_plus->resize(36, 36); }
        if (m_emoji) { m_emoji->move(inputR.x + 56, iconY); m_emoji->resize(36, 36); }
        if (m_msgEdit) { m_msgEdit->move(inputR.x + 98, inputR.y + inputPadY); m_msgEdit->resize(composerW, composerH); }
        if (m_mic) { m_mic->move(inputR.x + rightW - 46, iconY); m_mic->resize(36, 36); }
    }

    updateEmojiPopupLayout_();
    updateAttachPopupLayout_();

    if (m_loginPage) {
        m_loginPage->move(baseX, baseY);
        m_loginPage->resize(w, h);
    }
}

SwString WhatsAppWidget::fireUserId_() const {
    SwString uid = envStr_("SW_FIREBD_UID").trimmed();
    if (!uid.isEmpty()) {
        return uid;
    }

    const SwString sessionPhone = normalizePhone_(settingsStr_("session/phone"));
    if (!sessionPhone.isEmpty()) {
        return sessionPhone;
    }
    if (!m_store) {
        return SwString();
    }
    const WaLocalStore::UserProfile& user = m_store->user();
    if (!user.phone.trimmed().isEmpty()) {
        return normalizePhone_(user.phone);
    }
    if (!user.displayName.trimmed().isEmpty()) {
        return user.displayName.trimmed();
    }
    return SwString();
}

bool WhatsAppWidget::ensureFireService_() {
    const SwString baseUrl = fireBdUrl_();
    if (baseUrl.isEmpty()) {
        return false;
    }
    const SwString uid = fireUserId_();
    if (uid.isEmpty()) {
        return false;
    }

    if (!m_fire) {
        m_fire = new FireBDChatService(this);
        SwObject::connect(m_fire, &FireBDChatService::incomingMessages, this, [this](const SwList<FireBDMessage>& msgs) {
            onFireIncoming_(msgs);
        });
        SwObject::connect(m_fire, &FireBDChatService::statusEvents, this, [this](const SwList<FireBDStatusEvent>& evs) {
            onFireStatusEvents_(evs);
        });
        SwObject::connect(m_fire,
                          &FireBDChatService::outgoingMessageResult,
                          this,
                          [this](const SwString& messageId, bool ok) { onFireOutgoingResult_(messageId, ok); });
    }

    m_fire->setDatabaseUrl(baseUrl);
    m_fire->setAuthToken(fireBdAuth_());
    m_fire->setUserId(uid);

    const int pollMs = fireBdPollMs_();
    if (pollMs > 0) {
        m_fire->setPollIntervalMs(pollMs);
    }

    return m_fire->start();
}

bool WhatsAppWidget::ensureUserService_() {
    const SwString baseUrl = fireBdUrl_();
    if (baseUrl.isEmpty()) {
        return false;
    }

    if (!m_userService) {
        m_userService = new FireBDUserService(this);
        SwObject::connect(m_userService,
                          &FireBDUserService::loginFinished,
                          this,
                          [this](bool ok, const FireBDUser& user, const SwString& err) { onUserLoginFinished_(ok, user, err); });
        SwObject::connect(m_userService,
                          &FireBDUserService::signUpFinished,
                          this,
                          [this](bool ok, const FireBDUser& user, const SwString& err) { onUserSignUpFinished_(ok, user, err); });
    }

    m_userService->setDatabaseUrl(baseUrl);
    m_userService->setAuthToken(fireBdAuth_());
    return true;
}

void WhatsAppWidget::persistFireConfigFromLoginPage_() {
    if (!m_loginPage) {
        return;
    }

    const SwString url = m_loginPage->databaseUrl().trimmed();
    if (!url.isEmpty()) {
        waSettings_().setValue("firebd/url", SwAny(url));
    }

    const SwString token = m_loginPage->authToken().trimmed();
    if (!token.isEmpty()) {
        waSettings_().setValue("firebd/auth", SwAny(token));
    } else {
        waSettings_().remove("firebd/auth");
    }

    const int pollMs = m_loginPage->pollIntervalMs();
    if (pollMs > 0) {
        waSettings_().setValue("firebd/pollMs", SwAny(pollMs));
    } else {
        waSettings_().remove("firebd/pollMs");
    }

    waSettings_().sync();
}

void WhatsAppWidget::restoreSessionOrShowLogin_() {
    if (m_loginPage) {
        m_loginPage->setDatabaseUrl(fireBdUrl_());
        m_loginPage->setAuthToken(fireBdAuth_());
        const int pollMs = fireBdPollMs_();
        if (pollMs > 0) {
            m_loginPage->setPollIntervalMs(pollMs);
        }
        m_loginPage->setErrorText(SwString());
    }

    if (fireBdUrl_().isEmpty()) {
        setLoggedIn_(false);
        if (m_loginPage) {
            m_loginPage->setErrorText("Firebase URL manquante.");
        }
        return;
    }

    const bool loggedIn = settingsBool_("session/loggedIn", false);
    const SwString phone = normalizePhone_(settingsStr_("session/phone"));
    if (!loggedIn || phone.isEmpty()) {
        setLoggedIn_(false);
        return;
    }

    const SwString pseudo = settingsStr_("session/pseudo");
    const SwString firstName = settingsStr_("session/firstName");
    const SwString lastName = settingsStr_("session/lastName");

    SwString displayName = pseudo.trimmed();
    if (displayName.isEmpty()) {
        displayName = firstName.trimmed();
        if (!lastName.trimmed().isEmpty()) {
            if (!displayName.isEmpty()) displayName += " ";
            displayName += lastName.trimmed();
        }
    }
    if (displayName.isEmpty()) {
        displayName = phone;
    }

    if (m_store) {
        WaLocalStore::UserProfile& user = m_store->user();
        user.loggedIn = true;
        user.displayName = displayName;
        user.phone = phone;
        user.initial = user.displayName.left(1).toUpper();
        if (user.avatarColor.r == 0 && user.avatarColor.g == 0 && user.avatarColor.b == 0) {
            user.avatarColor = SwColor{0, 168, 132};
        }
        m_store->save();

        if (m_navProfile) {
            if (!user.initial.isEmpty()) {
                m_navProfile->setInitial(user.initial);
            }
            m_navProfile->setColor(user.avatarColor);
        }
    }

    setLoggedIn_(true);
}

void WhatsAppWidget::onAuthLoginRequested_(const SwString& idOrPhone, const SwString& password) {
    if (m_loginPage) {
        m_loginPage->setErrorText(SwString());
    }
    persistFireConfigFromLoginPage_();
    if (!ensureUserService_() || !m_userService) {
        if (m_loginPage) {
            m_loginPage->setErrorText("Firebase non configuré (URL manquante).");
        }
        return;
    }
    m_userService->logIn(idOrPhone, password);
}

void WhatsAppWidget::onAuthSignUpRequested_(const SwString& firstName,
                                           const SwString& lastName,
                                           const SwString& pseudo,
                                           const SwString& phone,
                                           const SwString& password) {
    if (m_loginPage) {
        m_loginPage->setErrorText(SwString());
    }
    persistFireConfigFromLoginPage_();
    if (!ensureUserService_() || !m_userService) {
        if (m_loginPage) {
            m_loginPage->setErrorText("Firebase non configuré (URL manquante).");
        }
        return;
    }
    m_userService->signUp(firstName, lastName, pseudo, phone, password);
}

void WhatsAppWidget::onUserLoginFinished_(bool ok, const FireBDUser& user, const SwString& error) {
    if (!ok) {
        if (m_loginPage) {
            m_loginPage->setErrorText(error.isEmpty() ? SwString("Connexion impossible.") : error);
        }
        return;
    }

    const SwString phone = normalizePhone_(user.phone);
    if (phone.isEmpty()) {
        if (m_loginPage) {
            m_loginPage->setErrorText("Compte invalide (téléphone).");
        }
        return;
    }

    waSettings_().setValue("session/loggedIn", SwAny(true));
    waSettings_().setValue("session/phone", SwAny(phone));
    waSettings_().setValue("session/pseudo", SwAny(user.pseudo.trimmed()));
    waSettings_().setValue("session/firstName", SwAny(user.firstName.trimmed()));
    waSettings_().setValue("session/lastName", SwAny(user.lastName.trimmed()));
    waSettings_().sync();

    if (m_store) {
        WaLocalStore::UserProfile& local = m_store->user();
        local.loggedIn = true;
        local.phone = phone;
        local.displayName = user.displayName();
        if (local.displayName.isEmpty()) {
            local.displayName = phone;
        }
        local.initial = local.displayName.left(1).toUpper();
        if (local.avatarColor.r == 0 && local.avatarColor.g == 0 && local.avatarColor.b == 0) {
            local.avatarColor = SwColor{0, 168, 132};
        }
        m_store->save();

        if (m_navProfile) {
            if (!local.initial.isEmpty()) {
                m_navProfile->setInitial(local.initial);
            }
            m_navProfile->setColor(local.avatarColor);
        }
    }

    setLoggedIn_(true);
}

void WhatsAppWidget::onUserSignUpFinished_(bool ok, const FireBDUser& user, const SwString& error) {
    if (!ok) {
        if (m_loginPage) {
            m_loginPage->setErrorText(error.isEmpty() ? SwString("Création de compte impossible.") : error);
        }
        return;
    }
    onUserLoginFinished_(true, user, SwString());
}

void WhatsAppWidget::logout_() {
    if (!m_loggedIn) {
        return;
    }

    if (m_fire) {
        m_fire->stop();
    }

    waSettings_().remove("session");
    waSettings_().sync();

    if (m_store) {
        WaLocalStore::UserProfile& local = m_store->user();
        local.loggedIn = false;
        local.displayName = SwString();
        local.phone = SwString();
        local.initial = SwString();
        m_store->save();
    }

    setLoggedIn_(false);
    if (m_loginPage) {
        m_loginPage->setDatabaseUrl(fireBdUrl_());
        m_loginPage->setAuthToken(fireBdAuth_());
        const int pollMs = fireBdPollMs_();
        if (pollMs > 0) {
            m_loginPage->setPollIntervalMs(pollMs);
        }
        m_loginPage->setErrorText(SwString());
    }
}

void WhatsAppWidget::openNewChat_() {
    if (!m_loggedIn || !m_store) {
        return;
    }
    if (!ensureUserService_() || !m_userService) {
        return;
    }

    auto* dlg = new SwDialog(this);
    dlg->setWindowTitle("Nouvelle discussion");
    SwObject::connect(dlg, &SwDialog::finished, [dlg](int) { delete dlg; });

    SwLineEdit* phoneEdit = new SwLineEdit("Téléphone du contact", dlg->contentWidget());
    phoneEdit->setStyleSheet(R"(
        SwLineEdit {
            background-color: rgb(255, 255, 255);
            border-color: rgb(220, 224, 232);
            border-width: 1px;
            border-radius: 12px;
            padding: 8px 12px;
            color: rgb(17, 27, 33);
        }
        SwLineEdit:focus { border-color: rgb(0, 168, 132); }
    )");

    SwLabel* err = new SwLabel("", dlg->contentWidget());
    err->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(220, 38, 38); font-size: 12px; }");
    err->setVisible(false);

    SwButton* cancel = new SwButton("Annuler", dlg->buttonBarWidget());
    SwButton* create = new SwButton("Créer", dlg->buttonBarWidget());
    create->setStyleSheet(R"(
        SwButton {
            background-color: rgb(0, 168, 132);
            border-width: 0px;
            border-radius: 14px;
            color: rgb(255, 255, 255);
            font-size: 14px;
            padding: 10px 14px;
        }
        SwButton:hover { background-color: rgb(0, 160, 125); }
        SwButton:pressed { background-color: rgb(0, 150, 115); }
    )");

    auto layout = [dlg, phoneEdit, err, cancel, create]() {
        if (!dlg || !phoneEdit || !err || !cancel || !create) {
            return;
        }
        const SwRect cr = dlg->contentWidget() ? dlg->contentWidget()->getRect() : SwRect{};
        const SwRect br = dlg->buttonBarWidget() ? dlg->buttonBarWidget()->getRect() : SwRect{};

        const int fieldH = 40;
        const int pad = 0;
        phoneEdit->move(cr.x + pad, cr.y + pad);
        phoneEdit->resize(std::max(0, cr.width - 2 * pad), fieldH);

        err->move(cr.x + pad, cr.y + pad + fieldH + 8);
        err->resize(std::max(0, cr.width - 2 * pad), 18);

        const int btnH = 40;
        const int btnW = 120;
        cancel->move(br.x, br.y + (br.height - btnH) / 2);
        cancel->resize(btnW, btnH);
        create->move(br.x + std::max(0, br.width - btnW), br.y + (br.height - btnH) / 2);
        create->resize(btnW, btnH);
    };

    SwObject::connect(dlg, &SwDialog::shown, [layout]() { layout(); });
    SwObject::connect(dlg, &SwWidget::resized, [layout](int, int) { layout(); });

    auto expectedPhone = std::make_shared<SwString>();
    auto inFlight = std::make_shared<bool>(false);

    SwObject::connect(m_userService,
                      &FireBDUserService::lookupFinished,
                      dlg,
                      [this, dlg, expectedPhone, inFlight, err, create](bool ok, const FireBDUser& found, const SwString& lookupErr) {
                          if (!dlg || !expectedPhone || !inFlight || !err || !create) {
                              return;
                          }
                          if (!*inFlight) {
                              return;
                          }
                          const SwString phone = expectedPhone->trimmed();
                          if (phone.isEmpty()) {
                              return;
                          }

                          if (ok) {
                              const SwString got = normalizePhone_(found.phone);
                              if (!got.isEmpty() && got != phone) {
                                  return; // not our request
                              }
                          }

                          *inFlight = false;

                          if (!ok) {
                              create->setVisible(true);
                              err->setText(lookupErr == "NOT_FOUND" ? SwString("Utilisateur introuvable.") : (lookupErr.isEmpty() ? SwString("Erreur réseau.") : lookupErr));
                              err->setVisible(true);
                              return;
                          }

                          SwString displayName = found.displayName().trimmed();
                          if (displayName.isEmpty()) {
                              displayName = phone;
                          }

                          m_store->ensureDirectConversation(phone, displayName);
                          m_store->setSelectedConversationId(phone);
                          m_store->save();

                          buildConversations_(false);
                          setCurrentConversationId_(phone);
                          buildThread_();

                          dlg->accept();
                      });

    SwObject::connect(cancel, &SwButton::clicked, [dlg]() { dlg->reject(); });
    SwObject::connect(create, &SwButton::clicked, [this, dlg, phoneEdit, err, create, expectedPhone, inFlight]() {
        if (!dlg || !phoneEdit || !err || !create || !expectedPhone || !inFlight) {
            return;
        }
        if (*inFlight) {
            return;
        }
        const SwString raw = phoneEdit->getText().trimmed();
        const SwString phone = normalizePhone_(raw);
        if (phone.isEmpty()) {
            err->setText("Téléphone invalide.");
            err->setVisible(true);
            phoneEdit->setFocus(true);
            return;
        }

        *expectedPhone = phone;
        *inFlight = true;
        create->setVisible(false);
        err->setText("Recherche...");
        err->setVisible(true);

        m_userService->lookupUserByPhone(phone);
    });

    dlg->open();
}

void WhatsAppWidget::onFireIncoming_(const SwList<FireBDMessage>& messages) {
    if (!m_store || messages.isEmpty()) {
        return;
    }

    bool changedAny = false;
    bool rebuildConversations = false;
    bool rebuildCurrent = false;
    for (int i = 0; i < messages.size(); ++i) {
        const FireBDMessage& msg = messages[i];

        SwString conversationId = msg.fromUserId.trimmed();
        if (conversationId.isEmpty()) {
            conversationId = msg.conversationId.trimmed();
        }
        if (conversationId.isEmpty()) {
            continue;
        }

        if (!m_store->conversationById(conversationId)) {
            m_store->ensureDirectConversation(conversationId, conversationId);
            rebuildConversations = true;
        }

        SwString localKind = msg.kind.toLower().trimmed();
        if (localKind == "text") {
            localKind = SwString();
        }

        const SwString meta = msg.meta.isEmpty() ? nowHm_() : msg.meta;

        m_store->appendMessage(conversationId,
                               "in",
                               msg.text,
                               meta,
                               localKind,
                               msg.payload,
                               SwString(),
                               msg.messageId,
                               msg.fromUserId,
                               "delivered");
        changedAny = true;

        if (conversationId == m_currentConversationId) {
            m_store->markRead(conversationId);
            sendReadAcksForConversation_(conversationId);
            rebuildCurrent = true;
        }

        refreshConversationRowUi_(conversationId);
    }

    if (changedAny) {
        m_store->save();
    }

    if (rebuildConversations) {
        buildConversations_(false);
    }

    if (m_conversationFilter == ConversationFilter::Unread) {
        buildConversations_(false);
    }

    if (rebuildCurrent) {
        buildThread_();
    }
}

void WhatsAppWidget::onFireStatusEvents_(const SwList<FireBDStatusEvent>& events) {
    if (!m_store || events.isEmpty()) {
        return;
    }

    bool changedAny = false;
    bool rebuildCurrent = false;
    for (int i = 0; i < events.size(); ++i) {
        const FireBDStatusEvent& ev = events[i];
        if (ev.messageId.isEmpty()) {
            continue;
        }

        SwString convoId;
        const SwString statusStr = fireBdStatusToString(ev.status);
        const bool changed = m_store->setMessageStatusByMessageId(ev.messageId, statusStr, &convoId);
        if (!changed) {
            continue;
        }

        changedAny = true;
        if (!convoId.isEmpty()) {
            refreshConversationRowUi_(convoId);
            if (convoId == m_currentConversationId) {
                rebuildCurrent = true;
            }
        }
    }

    if (changedAny) {
        m_store->save();
    }
    if (rebuildCurrent) {
        buildThread_();
    }
}

void WhatsAppWidget::onFireOutgoingResult_(const SwString& messageId, bool ok) {
    if (!m_store || messageId.isEmpty()) {
        return;
    }
    if (ok) {
        return;
    }

    SwString convoId;
    if (m_store->setMessageStatusByMessageId(messageId, SwString(), &convoId)) {
        m_store->save();
        if (!convoId.isEmpty()) {
            refreshConversationRowUi_(convoId);
            if (convoId == m_currentConversationId) {
                buildThread_();
            }
        }
    }
}

void WhatsAppWidget::sendReadAcksForConversation_(const SwString& conversationId) {
    if (!m_store || conversationId.isEmpty()) {
        return;
    }
    if (!m_loggedIn) {
        return;
    }
    if (!ensureFireService_() || !m_fire) {
        return;
    }

    WaLocalStore::Conversation* c = m_store->conversationById(conversationId);
    if (!c) {
        return;
    }

    bool changedAny = false;
    for (int i = 0; i < c->messages.size(); ++i) {
        WaLocalStore::Message& m = c->messages[i];
        if (m.role != "in") {
            continue;
        }
        if (m.messageId.isEmpty() || m.fromUserId.isEmpty()) {
            continue;
        }
        if (m.status.toLower() == "read") {
            continue;
        }
        m_fire->sendMessageStatus(m.fromUserId, conversationId, m.messageId, FireBDMessageStatus::Read);
        m.status = "read";
        changedAny = true;
    }

    if (changedAny) {
        refreshConversationRowUi_(conversationId);
        m_store->save();
    }
}
