#pragma once

#include "Sw.h"
#include "SwList.h"
#include "SwMap.h"
#include "SwString.h"

class WaLocalStore {
public:
    struct UserProfile {
        bool loggedIn{false};
        SwString displayName;
        SwString phone;
        SwString initial;
        SwColor avatarColor{0, 168, 132};
    };

    struct Contact {
        SwString id;
        SwString displayName;
        SwString phone;
        SwString status;
        SwString initial;
        SwColor avatarColor{100, 116, 139};
    };

    struct Message {
        SwString messageId;   // firebase/message id (optional)
        SwString fromUserId;  // sender id (optional, used for status acks)
        SwString status;      // "sent" | "delivered" | "read" | "" (optional)
        SwString text;
        SwString role;      // "in" | "out"
        SwString kind;      // "" | "image" | ...
        SwString payload;   // optional: media/file path (usually relative to DB root)
        SwString meta;      // time string
        SwString reaction;  // optional
    };

    struct Conversation {
        SwString id;
        SwString type;  // "direct" | "group"
        SwString title; // used for groups
        SwString status;
        SwString avatarInitial;
        SwColor avatarColor{100, 116, 139};
        SwList<SwString> participantIds;
        bool favorite{false};
        int unreadCount{0};
        SwList<Message> messages;

        SwString lastPreviewText() const;
        SwString lastTimeText() const;
    };

    struct UiState {
        SwString selectedConversationId;
        SwMap<SwString, SwString> draftsByConversationId;
    };

    // If storageRoot is provided, the DB will be stored inside it (useful for snapshots).
    explicit WaLocalStore(const SwString& storageRoot = SwString());

    bool loadOrSeed();
    bool save() const;

    const SwString& filePath() const { return m_filePath; }

    const UserProfile& user() const { return m_user; }
    UserProfile& user() { return m_user; }

    const SwList<Contact>& contacts() const { return m_contacts; }
    Contact* contactById(const SwString& id);
    const Contact* contactById(const SwString& id) const;
    Contact* ensureContact(const SwString& id, const SwString& displayName = SwString());

    const SwList<Conversation>& conversations() const { return m_conversations; }
    Conversation* conversationById(const SwString& id);
    const Conversation* conversationById(const SwString& id) const;
    Conversation* ensureDirectConversation(const SwString& id, const SwString& displayName = SwString());

    // Presentation helpers (computed from contacts + conversations).
    SwString titleForConversationId(const SwString& conversationId) const;
    SwString statusForConversationId(const SwString& conversationId) const;
    SwString initialForConversationId(const SwString& conversationId) const;
    SwColor avatarColorForConversationId(const SwString& conversationId) const;

    SwString selectedConversationId() const { return m_ui.selectedConversationId; }
    void setSelectedConversationId(const SwString& id);

    SwString draftForConversationId(const SwString& conversationId) const;
    void setDraftForConversationId(const SwString& conversationId, const SwString& text);

    // Media helpers (payload is stored relative to the DB root when possible).
    SwString resolveMediaPath(const SwString& payload) const;
    SwString importMediaFile(const SwString& sourcePath) const;

    bool appendMessage(const SwString& conversationId,
                       const SwString& role,
                       const SwString& text,
                       const SwString& meta,
                       const SwString& kind = SwString(),
                       const SwString& payload = SwString(),
                       const SwString& reaction = SwString(),
                       const SwString& messageId = SwString(),
                       const SwString& fromUserId = SwString(),
                       const SwString& status = SwString());

    bool markRead(const SwString& conversationId);

    // Update a message status given its messageId; scans all conversations.
    bool setMessageStatusByMessageId(const SwString& messageId, const SwString& status, SwString* outConversationId = nullptr);

private:
    static SwString normalizeRoot_(SwString root);
    static SwString joinPath_(const SwString& base, const SwString& child);
    static SwString parentDirectory_(SwString path);
    static bool ensureDirectory_(const SwString& path);

    bool loadFromDisk_();
    bool loadFromJson_(const SwString& json);
    void seedDefault_();
    bool selectedIdExists_() const;

    void clear_();

    SwString m_filePath;
    UserProfile m_user;
    SwList<Contact> m_contacts;
    SwList<Conversation> m_conversations;
    UiState m_ui;
};
