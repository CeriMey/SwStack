#pragma once

#include "SwWidget.h"

#include "SwList.h"
#include "SwString.h"

class SwButton;
class SwFrame;
class SwLabel;
class SwLineEdit;
class SwListView;
class SwListWidget;
class SwStandardItemModel;
class SwChatBubble;

class WaAvatarCircle;
class WaAttachMenuPopup;
class WaChatWallpaper;
class WaConversationListDelegate;
class WaConversationRowWidget;
class WaEmojiPickerPopup;
class WaIconButton;
class WaLoginPage;
class WaLocalStore;
class WaMessageEdit;
class WaThreadBubbleDelegate;
class FireBDChatService;
class FireBDUserService;
struct FireBDMessage;
struct FireBDStatusEvent;
struct FireBDUser;

class WhatsAppWidget final : public SwWidget {
    SW_OBJECT(WhatsAppWidget, SwWidget)

public:
    explicit WhatsAppWidget(SwWidget* parent = nullptr, const SwString& storageRoot = SwString());
    ~WhatsAppWidget() override;

    SwListWidget* conversationList() const { return m_convoList; }
    SwListView* threadView() const { return m_threadView; }

    // Snapshot helpers (used by WhatsAppWindow::saveSnapshot).
    void setLoggedInForSnapshot(bool on) { setLoggedIn_(on); }
    void setAttachMenuForSnapshot(bool on) { setAttachPopupVisible_(on); }
    void setEmojiPopupForSnapshot(bool on) { setEmojiPopupVisible_(on); }
    void setComposerTextForSnapshot(const SwString& text);
    void appendTextMessageForSnapshot(const SwString& text);
    void appendMediaMessageForSnapshot(const SwString& kind, const SwString& payload);
    void setLastTextSelectionForSnapshot(int start, int end);

protected:
    void resizeEvent(ResizeEvent* event) override;
    void mousePressEvent(MouseEvent* event) override;
    void keyPressEvent(KeyEvent* event) override;

private:
    enum class ConversationFilter {
        All,
        Unread,
        Favorites,
        Groups
    };

    void buildUi_();
    void buildConversations_(bool allowFallbackSelection = true);
    void buildThread_();
    void updateLayout_();
    void updateEmojiPopupLayout_();
    void updateAttachPopupLayout_();

    void setConversationFilter_(ConversationFilter filter);
    void updateConversationFilterUi_();

    void setEmojiPopupVisible_(bool on);
    bool isEmojiPopupVisible_() const;
    void toggleEmojiPopup_();

    void setAttachPopupVisible_(bool on);
    bool isAttachPopupVisible_() const;
    void toggleAttachPopup_();

    void setLoggedIn_(bool on);

    void updateInputActions_();
    void sendMessage_();
    void appendOutgoingMessage_(const SwString& text, const SwString& kind, const SwString& payload);
    void refreshConversationRowUi_(const SwString& conversationId);
    void setCurrentConversationId_(const SwString& conversationId);
    int rowForConversationId_(const SwString& conversationId) const;
    void onFireIncoming_(const SwList<FireBDMessage>& messages);
    void onFireStatusEvents_(const SwList<FireBDStatusEvent>& events);
    void onFireOutgoingResult_(const SwString& messageId, bool ok);
    void sendReadAcksForConversation_(const SwString& conversationId);
    SwString fireUserId_() const;
    bool ensureFireService_();
    bool ensureUserService_();

    void persistFireConfigFromLoginPage_();
    void restoreSessionOrShowLogin_();
    void onAuthLoginRequested_(const SwString& idOrPhone, const SwString& password);
    void onAuthSignUpRequested_(const SwString& firstName,
                               const SwString& lastName,
                               const SwString& pseudo,
                               const SwString& phone,
                               const SwString& password);
    void onUserLoginFinished_(bool ok, const FireBDUser& user, const SwString& error);
    void onUserSignUpFinished_(bool ok, const FireBDUser& user, const SwString& error);
    void logout_();

    void openNewChat_();

    SwFrame* m_nav{nullptr};
    SwFrame* m_sepNav{nullptr};
    SwFrame* m_left{nullptr};
    SwFrame* m_sepMid{nullptr};
    SwFrame* m_right{nullptr};

    // Nav
    WaIconButton* m_navChats{nullptr};
    WaIconButton* m_navCalls{nullptr};
    WaIconButton* m_navStatus{nullptr};
    WaAvatarCircle* m_navProfile{nullptr};
    WaIconButton* m_navSettings{nullptr};

    // Left top
    SwFrame* m_leftTop{nullptr};
    SwLabel* m_title{nullptr};
    WaIconButton* m_newChat{nullptr};
    WaIconButton* m_menu{nullptr};
    SwLineEdit* m_search{nullptr};
    WaIconButton* m_searchIcon{nullptr};
    SwButton* m_chipAll{nullptr};
    SwButton* m_chipUnread{nullptr};
    SwButton* m_chipFav{nullptr};
    SwButton* m_chipGroups{nullptr};

    // Conversation list
    SwListWidget* m_convoList{nullptr};
    WaConversationListDelegate* m_convoDelegate{nullptr};

    // Right header
    SwFrame* m_header{nullptr};
    WaAvatarCircle* m_headerAvatar{nullptr};
    SwLabel* m_headerName{nullptr};
    SwLabel* m_headerStatus{nullptr};
    WaIconButton* m_btnVideo{nullptr};
    WaIconButton* m_btnPhone{nullptr};
    WaIconButton* m_btnSearch{nullptr};
    WaIconButton* m_btnMore{nullptr};

    // Chat + input
    WaChatWallpaper* m_chatBg{nullptr};
    SwListView* m_threadView{nullptr};
    WaThreadBubbleDelegate* m_threadDelegate{nullptr};
    SwChatBubble* m_lastThreadTextBubble{nullptr};

    SwFrame* m_input{nullptr};
    WaIconButton* m_plus{nullptr};
    WaIconButton* m_emoji{nullptr};
    WaMessageEdit* m_msgEdit{nullptr};
    WaIconButton* m_mic{nullptr};

    WaEmojiPickerPopup* m_emojiPopup{nullptr};
    WaAttachMenuPopup* m_attachPopup{nullptr};
    WaLoginPage* m_loginPage{nullptr};
    bool m_loggedIn{false};

    SwStandardItemModel* m_threadModel{nullptr};

    WaLocalStore* m_store{nullptr};
    FireBDChatService* m_fire{nullptr};
    FireBDUserService* m_userService{nullptr};
    SwString m_currentConversationId;
    SwList<SwString> m_conversationIds;
    SwList<WaConversationRowWidget*> m_conversationRows;
    ConversationFilter m_conversationFilter{ConversationFilter::All};
};
