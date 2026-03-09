#include "WaLocalStore.h"

#include "SwDir.h"
#include "SwFile.h"
#include "SwFileInfo.h"
#include "SwJsonArray.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwJsonValue.h"
#include "SwStandardPaths.h"
#include "SwDateTime.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

static constexpr int kWaLocalStoreJsonVersion = 4;

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

static SwString toSwString_(const SwJsonValue& v) {
    if (!v.isString()) {
        return SwString();
    }
    return SwString(v.toString());
}

static SwColor toColor_(const SwJsonValue& v, const SwColor& fallback) {
    if (!v.isArray()) {
        return fallback;
    }
    const SwJsonArray arr = v.toArray();
    if (arr.size() < 3) {
        return fallback;
    }
    SwColor c = fallback;
    c.r = arr[0].toInt();
    c.g = arr[1].toInt();
    c.b = arr[2].toInt();
    return c;
}

static SwJsonValue fromColor_(const SwColor& c) {
    SwJsonArray arr;
    arr.append(SwJsonValue(c.r));
    arr.append(SwJsonValue(c.g));
    arr.append(SwJsonValue(c.b));
    return SwJsonValue(arr);
}

static SwJsonValue fromStringList_(const SwList<SwString>& list) {
    SwJsonArray arr;
    for (int i = 0; i < list.size(); ++i) {
        arr.append(SwJsonValue(list[i].toStdString()));
    }
    return SwJsonValue(arr);
}

static SwList<SwString> toStringList_(const SwJsonValue& v) {
    SwList<SwString> out;
    if (!v.isArray()) {
        return out;
    }
    const SwJsonArray arr = v.toArray();
    for (const SwJsonValue& item : arr) {
        if (!item.isString()) {
            continue;
        }
        out.append(SwString(item.toString()));
    }
    return out;
}

SwString WaLocalStore::Conversation::lastPreviewText() const {
    if (messages.isEmpty()) {
        return SwString();
    }
    const Message& m = messages[messages.size() - 1];
    if (m.kind == "image") {
        return SwString("Photo");
    }
    if (m.kind == "video") {
        return SwString("Vidéo");
    }
    return m.text;
}

SwString WaLocalStore::Conversation::lastTimeText() const {
    if (messages.isEmpty()) {
        return SwString();
    }
    return messages[messages.size() - 1].meta;
}

WaLocalStore::WaLocalStore(const SwString& storageRoot) {
    if (!storageRoot.isEmpty()) {
        SwString root = normalizeRoot_(storageRoot);
        ensureDirectory_(root);
        m_filePath = joinPath_(root, "whatsapp_db.json");
        return;
    }

    SwString base = SwStandardPaths::writableLocation(SwStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = SwStandardPaths::writableLocation(SwStandardPaths::AppConfigLocation);
    }
    if (base.isEmpty()) {
        base = SwStandardPaths::writableLocation(SwStandardPaths::HomeLocation);
    }
    if (base.isEmpty()) {
        base = "./";
    }

    SwString appDir = "WhatsApp";
    const SwString profile = envStr_("SW_WA_PROFILE").trimmed();
    if (!profile.isEmpty()) {
        appDir += "_" + profile;
    }

    SwString dir = joinPath_(joinPath_(base, "SwCore"), appDir);
    ensureDirectory_(dir);
    m_filePath = joinPath_(dir, "whatsapp_db.json");
}

void WaLocalStore::clear_() {
    m_user = UserProfile();
    m_contacts.clear();
    m_conversations.clear();
    m_ui = UiState();
}

bool WaLocalStore::loadOrSeed() {
    clear_();

    if (!loadFromDisk_()) {
        // Backward compat: previous file name used by the WhatsApp example.
        const SwString legacyPath = joinPath_(parentDirectory_(m_filePath), "conversations.json");
        if (!legacyPath.isEmpty() && legacyPath != m_filePath) {
            SwFileInfo legacyInfo(legacyPath.toStdString());
            if (legacyInfo.exists() && !legacyInfo.isDir()) {
                SwFile in(legacyPath);
                if (in.open(SwFile::Read)) {
                    const SwString json = in.readAll();
                    in.close();
                    if (!json.isEmpty() && loadFromJson_(json)) {
                        if (!m_conversations.isEmpty() && !selectedIdExists_()) {
                            m_ui.selectedConversationId = m_conversations[0].id;
                        }
                        return save();
                    }
                }
            }
        }

        seedDefault_();
        return save();
    }

    if (m_conversations.isEmpty()) {
        seedDefault_();
        return save();
    }
    if (!selectedIdExists_()) {
        m_ui.selectedConversationId = m_conversations[0].id;
        return save();
    }
    return true;
}

bool WaLocalStore::save() const {
    if (m_filePath.isEmpty()) {
        return false;
    }

    SwJsonObject root;
    root["version"] = SwJsonValue(kWaLocalStoreJsonVersion);

    // User profile (login state + identity).
    SwJsonObject user;
    user["loggedIn"] = SwJsonValue(m_user.loggedIn);
    user["displayName"] = SwJsonValue(m_user.displayName.toStdString());
    user["phone"] = SwJsonValue(m_user.phone.toStdString());
    user["initial"] = SwJsonValue(m_user.initial.toStdString());
    user["avatarColor"] = fromColor_(m_user.avatarColor);
    root["user"] = SwJsonValue(user);

    // Contacts.
    SwJsonArray contacts;
    for (int i = 0; i < m_contacts.size(); ++i) {
        const Contact& c = m_contacts[i];
        SwJsonObject co;
        co["id"] = SwJsonValue(c.id.toStdString());
        co["displayName"] = SwJsonValue(c.displayName.toStdString());
        co["phone"] = SwJsonValue(c.phone.toStdString());
        co["status"] = SwJsonValue(c.status.toStdString());
        co["initial"] = SwJsonValue(c.initial.toStdString());
        co["avatarColor"] = fromColor_(c.avatarColor);
        contacts.append(SwJsonValue(co));
    }
    root["contacts"] = SwJsonValue(contacts);

    // Conversations.
    SwJsonArray conversations;
    for (int i = 0; i < m_conversations.size(); ++i) {
        const Conversation& c = m_conversations[i];

        SwJsonObject co;
        co["id"] = SwJsonValue(c.id.toStdString());
        co["type"] = SwJsonValue(c.type.toStdString());
        co["title"] = SwJsonValue(c.title.toStdString());
        co["status"] = SwJsonValue(c.status.toStdString());
        co["avatarInitial"] = SwJsonValue(c.avatarInitial.toStdString());
        co["avatarColor"] = fromColor_(c.avatarColor);
        co["participantIds"] = fromStringList_(c.participantIds);
        co["favorite"] = SwJsonValue(c.favorite);
        co["unreadCount"] = SwJsonValue(c.unreadCount);

        SwJsonArray msgs;
        for (int j = 0; j < c.messages.size(); ++j) {
            const Message& m = c.messages[j];
            SwJsonObject mo;
            mo["messageId"] = SwJsonValue(m.messageId.toStdString());
            mo["fromUserId"] = SwJsonValue(m.fromUserId.toStdString());
            mo["status"] = SwJsonValue(m.status.toStdString());
            mo["text"] = SwJsonValue(m.text.toStdString());
            mo["role"] = SwJsonValue(m.role.toStdString());
            mo["kind"] = SwJsonValue(m.kind.toStdString());
            mo["payload"] = SwJsonValue(m.payload.toStdString());
            mo["meta"] = SwJsonValue(m.meta.toStdString());
            mo["reaction"] = SwJsonValue(m.reaction.toStdString());
            msgs.append(SwJsonValue(mo));
        }
        co["messages"] = SwJsonValue(msgs);

        conversations.append(SwJsonValue(co));
    }
    root["conversations"] = SwJsonValue(conversations);

    // UI state.
    SwJsonObject ui;
    ui["selectedConversationId"] = SwJsonValue(m_ui.selectedConversationId.toStdString());

    SwJsonObject drafts;
    for (auto it = m_ui.draftsByConversationId.begin(); it != m_ui.draftsByConversationId.end(); ++it) {
        const SwString key = it.key();
        if (key.isEmpty()) {
            continue;
        }
        drafts[key] = SwJsonValue(it.value().toStdString());
    }
    ui["drafts"] = SwJsonValue(drafts);
    root["ui"] = SwJsonValue(ui);

    SwJsonDocument doc(root);
    const std::string json = doc.toJson();

    ensureDirectory_(parentDirectory_(m_filePath));
    SwFile out(m_filePath);
    if (!out.open(SwFile::Write)) {
        return false;
    }
    out.write(SwString(json));
    out.close();
    return true;
}

WaLocalStore::Contact* WaLocalStore::contactById(const SwString& id) {
    if (id.isEmpty()) {
        return nullptr;
    }
    for (int i = 0; i < m_contacts.size(); ++i) {
        if (m_contacts[i].id == id) {
            return &m_contacts[i];
        }
    }
    return nullptr;
}

const WaLocalStore::Contact* WaLocalStore::contactById(const SwString& id) const {
    if (id.isEmpty()) {
        return nullptr;
    }
    for (int i = 0; i < m_contacts.size(); ++i) {
        if (m_contacts[i].id == id) {
            return &m_contacts[i];
        }
    }
    return nullptr;
}

static SwColor colorForStableId_(const SwString& id) {
    // Small palette of pleasant colors (WhatsApp-ish).
    static const SwColor kPalette[] = {
        SwColor{82, 196, 26},   // green
        SwColor{14, 116, 144},  // teal
        SwColor{56, 189, 248},  // sky
        SwColor{244, 63, 94},   // rose
        SwColor{2, 132, 199},   // cyan
        SwColor{245, 158, 11},  // amber
        SwColor{139, 92, 246},  // violet
        SwColor{100, 116, 139}  // slate
    };

    const std::string s = id.toStdString();
    std::uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= static_cast<std::uint32_t>(c);
        h *= 16777619u;
    }
    const size_t idx = static_cast<size_t>(h) % (sizeof(kPalette) / sizeof(kPalette[0]));
    return kPalette[idx];
}

WaLocalStore::Contact* WaLocalStore::ensureContact(const SwString& id, const SwString& displayName) {
    if (id.isEmpty()) {
        return nullptr;
    }
    if (Contact* existing = contactById(id)) {
        if (!displayName.isEmpty() && existing->displayName.isEmpty()) {
            existing->displayName = displayName;
            existing->initial = displayName.left(1).toUpper();
        }
        return existing;
    }

    Contact c;
    c.id = id;
    c.displayName = displayName.isEmpty() ? id : displayName;
    c.initial = c.displayName.left(1).toUpper();
    c.avatarColor = colorForStableId_(id);
    m_contacts.append(c);
    return &m_contacts[m_contacts.size() - 1];
}

WaLocalStore::Conversation* WaLocalStore::conversationById(const SwString& id) {
    if (id.isEmpty()) {
        return nullptr;
    }
    for (int i = 0; i < m_conversations.size(); ++i) {
        if (m_conversations[i].id == id) {
            return &m_conversations[i];
        }
    }
    return nullptr;
}

const WaLocalStore::Conversation* WaLocalStore::conversationById(const SwString& id) const {
    if (id.isEmpty()) {
        return nullptr;
    }
    for (int i = 0; i < m_conversations.size(); ++i) {
        if (m_conversations[i].id == id) {
            return &m_conversations[i];
        }
    }
    return nullptr;
}

WaLocalStore::Conversation* WaLocalStore::ensureDirectConversation(const SwString& id, const SwString& displayName) {
    if (id.isEmpty()) {
        return nullptr;
    }
    if (Conversation* existing = conversationById(id)) {
        if (existing->type.isEmpty()) {
            existing->type = "direct";
        }
        if (!displayName.isEmpty() && existing->title.isEmpty()) {
            existing->title = displayName;
        }
        ensureContact(id, displayName);
        if (existing->participantIds.isEmpty()) {
            existing->participantIds.append(id);
        }
        return existing;
    }

    ensureContact(id, displayName);

    Conversation c;
    c.id = id;
    c.type = "direct";
    c.title = displayName;
    c.avatarInitial = id.left(1).toUpper();
    c.avatarColor = colorForStableId_(id);
    c.participantIds.append(id);
    m_conversations.append(c);
    return &m_conversations[m_conversations.size() - 1];
}

static const WaLocalStore::Contact* primaryContactForConversation_(const WaLocalStore& store,
                                                                   const WaLocalStore::Conversation& convo) {
    if (convo.type != "direct") {
        return nullptr;
    }
    if (convo.participantIds.isEmpty()) {
        return nullptr;
    }
    return store.contactById(convo.participantIds[0]);
}

SwString WaLocalStore::titleForConversationId(const SwString& conversationId) const {
    const Conversation* convo = conversationById(conversationId);
    if (!convo) {
        return SwString();
    }

    if (convo->type == "group") {
        return convo->title;
    }

    if (const Contact* c = primaryContactForConversation_(*this, *convo)) {
        if (!c->displayName.isEmpty()) {
            return c->displayName;
        }
    }
    return convo->title;
}

SwString WaLocalStore::statusForConversationId(const SwString& conversationId) const {
    const Conversation* convo = conversationById(conversationId);
    if (!convo) {
        return SwString();
    }

    if (convo->type == "group") {
        if (!convo->status.isEmpty()) {
            return convo->status;
        }
        const size_t participantCount = convo->participantIds.size();
        return SwString::number(participantCount) + " participants";
    }

    if (const Contact* c = primaryContactForConversation_(*this, *convo)) {
        if (!c->status.isEmpty()) {
            return c->status;
        }
    }
    return convo->status;
}

SwString WaLocalStore::initialForConversationId(const SwString& conversationId) const {
    const Conversation* convo = conversationById(conversationId);
    if (!convo) {
        return SwString();
    }

    if (convo->type == "direct") {
        if (const Contact* c = primaryContactForConversation_(*this, *convo)) {
            if (!c->initial.isEmpty()) {
                return c->initial;
            }
            if (!c->displayName.isEmpty()) {
                return c->displayName.left(1).toUpper();
            }
        }
    }

    if (!convo->avatarInitial.isEmpty()) {
        return convo->avatarInitial;
    }

    const SwString title = titleForConversationId(conversationId);
    return title.left(1).toUpper();
}

SwColor WaLocalStore::avatarColorForConversationId(const SwString& conversationId) const {
    const Conversation* convo = conversationById(conversationId);
    if (!convo) {
        return SwColor{100, 116, 139};
    }

    if (convo->type == "direct") {
        if (const Contact* c = primaryContactForConversation_(*this, *convo)) {
            return c->avatarColor;
        }
    }
    return convo->avatarColor;
}

void WaLocalStore::setSelectedConversationId(const SwString& id) {
    m_ui.selectedConversationId = id;
}

SwString WaLocalStore::draftForConversationId(const SwString& conversationId) const {
    if (conversationId.isEmpty()) {
        return SwString();
    }
    return m_ui.draftsByConversationId.value(conversationId);
}

void WaLocalStore::setDraftForConversationId(const SwString& conversationId, const SwString& text) {
    if (conversationId.isEmpty()) {
        return;
    }
    if (text.isEmpty()) {
        m_ui.draftsByConversationId.remove(conversationId);
        return;
    }
    m_ui.draftsByConversationId.insert(conversationId, text);
}

SwString WaLocalStore::resolveMediaPath(const SwString& payload) const {
    if (payload.isEmpty()) {
        return SwString();
    }

    SwString p = payload;
    p.replace("\\", "/");

    const bool isDrivePath = (p.size() >= 2 && p[1] == ':');
    const bool isUnc = p.startsWith("//") || p.startsWith("\\\\");
    const bool isAbsolute = isDrivePath || isUnc || p.startsWith("/") || p.startsWith("\\");
    if (isAbsolute) {
        return payload;
    }

    const SwString root = parentDirectory_(m_filePath);
    if (root.isEmpty()) {
        return payload;
    }
    return joinPath_(root, p);
}

SwString WaLocalStore::importMediaFile(const SwString& sourcePath) const {
    if (sourcePath.isEmpty()) {
        return SwString();
    }

    SwFileInfo info(sourcePath.toStdString());
    if (!info.exists() || info.isDir()) {
        return SwString();
    }

    const SwString root = parentDirectory_(m_filePath);
    if (root.isEmpty()) {
        return SwString();
    }

    SwString ext = SwString(info.suffix()).toLower();
    if (ext.isEmpty()) {
        ext = "bin";
    }

    const SwString mediaDir = joinPath_(root, "media");
    ensureDirectory_(mediaDir);

    // Name: m_<epoch>_<rand>.<ext>
    const SwDateTime now;
    const SwString ts = SwString::number(static_cast<int>(now.toTimeT()));
    const SwString rnd = SwString::number(static_cast<int>(std::rand()));
    const SwString fileName = SwString("m_%1_%2.%3").arg(ts).arg(rnd).arg(ext);

    const SwString absDest = joinPath_(mediaDir, fileName);
    if (!SwFile::copy(sourcePath, absDest, true)) {
        return SwString();
    }

    // Store relative payload so the DB folder is portable.
    return joinPath_("media", fileName);
}

bool WaLocalStore::appendMessage(const SwString& conversationId,
                                 const SwString& role,
                                 const SwString& text,
                                 const SwString& meta,
                                 const SwString& kind,
                                 const SwString& payload,
                                 const SwString& reaction,
                                 const SwString& messageId,
                                 const SwString& fromUserId,
                                 const SwString& status) {
    Conversation* c = conversationById(conversationId);
    if (!c) {
        return false;
    }

    Message m;
    m.messageId = messageId;
    m.fromUserId = fromUserId;
    m.status = status;
    m.text = text;
    m.role = role;
    m.kind = kind;
    m.payload = payload;
    m.meta = meta;
    m.reaction = reaction;
    c->messages.append(m);

    if (role == "in") {
        c->unreadCount += 1;
    }
    return true;
}

bool WaLocalStore::markRead(const SwString& conversationId) {
    Conversation* c = conversationById(conversationId);
    if (!c) {
        return false;
    }
    c->unreadCount = 0;
    return true;
}

bool WaLocalStore::setMessageStatusByMessageId(const SwString& messageId, const SwString& status, SwString* outConversationId) {
    if (outConversationId) {
        *outConversationId = SwString();
    }
    if (messageId.isEmpty()) {
        return false;
    }

    for (int i = 0; i < m_conversations.size(); ++i) {
        Conversation& c = m_conversations[i];
        for (int j = 0; j < c.messages.size(); ++j) {
            Message& m = c.messages[j];
            if (m.messageId != messageId) {
                continue;
            }
            auto rank = [](const SwString& s) -> int {
                const SwString v = s.toLower();
                if (v == "sent") return 1;
                if (v == "delivered") return 2;
                if (v == "read") return 3;
                return 0;
            };

            if (rank(status) <= rank(m.status)) {
                if (outConversationId) {
                    *outConversationId = c.id;
                }
                return false;
            }
            m.status = status;
            if (outConversationId) {
                *outConversationId = c.id;
            }
            return true;
        }
    }
    return false;
}

SwString WaLocalStore::normalizeRoot_(SwString root) {
    if (root.isEmpty()) {
        return root;
    }
    root.replace("\\", "/");
    while (root.endsWith("/")) {
        root.chop(1);
    }
    return root;
}

SwString WaLocalStore::joinPath_(const SwString& base, const SwString& child) {
    if (base.isEmpty()) {
        return child;
    }
    if (child.isEmpty()) {
        return base;
    }
    SwString out(base);
    if (!out.endsWith("/") && !out.endsWith("\\")) {
        out.append("/");
    }
    out.append(child);
    return out;
}

SwString WaLocalStore::parentDirectory_(SwString path) {
    if (path.isEmpty()) {
        return path;
    }
    path.replace("\\", "/");
    while (path.endsWith("/") && path.size() > 1) {
        path.chop(1);
    }
    const size_t idx = path.lastIndexOf('/');
    if (idx == static_cast<size_t>(-1) || idx == 0) {
        return SwString();
    }
    return path.left(static_cast<int>(idx));
}

bool WaLocalStore::ensureDirectory_(const SwString& path) {
    if (path.isEmpty()) {
        return false;
    }
    return SwDir::mkpathAbsolute(path);
}

bool WaLocalStore::loadFromDisk_() {
    if (m_filePath.isEmpty()) {
        return false;
    }
    SwFileInfo info(m_filePath.toStdString());
    if (!info.exists() || info.isDir()) {
        return false;
    }
    SwFile in(m_filePath);
    if (!in.open(SwFile::Read)) {
        return false;
    }
    const SwString json = in.readAll();
    in.close();
    if (json.isEmpty()) {
        return false;
    }
    return loadFromJson_(json);
}

bool WaLocalStore::loadFromJson_(const SwString& json) {
    clear_();

    SwString err;
    const SwJsonDocument doc = SwJsonDocument::fromJson(json.toStdString(), err);
    if (!doc.isObject()) {
        return false;
    }

    const SwJsonObject root = doc.object();
    const int version = root["version"].toInt();

    // v1 (legacy): { selectedConversationId, conversations[] }.
    if (version <= 1) {
        m_ui.selectedConversationId = toSwString_(root["selectedConversationId"]);

        const SwJsonValue convsV = root["conversations"];
        if (convsV.isArray()) {
            const SwJsonArray arr = convsV.toArray();
            for (const SwJsonValue& item : arr) {
                if (!item.isObject()) {
                    continue;
                }
                const SwJsonObject co = item.toObject();

                Conversation c;
                c.id = toSwString_(co["id"]);
                c.type = "direct";
                c.title = toSwString_(co["title"]);
                c.status = toSwString_(co["status"]);
                c.avatarInitial = toSwString_(co["initial"]);
                c.avatarColor = toColor_(co["avatarColor"], SwColor{100, 116, 139});
                c.unreadCount = co["unreadCount"].toInt();
                c.participantIds.append(c.id); // contactId == conversationId (simple migration).

                Contact contact;
                contact.id = c.id;
                contact.displayName = c.title;
                contact.status = c.status;
                contact.initial = c.avatarInitial;
                contact.avatarColor = c.avatarColor;
                m_contacts.append(contact);

                const SwJsonValue msgsV = co["messages"];
                if (msgsV.isArray()) {
                    const SwJsonArray msgs = msgsV.toArray();
                    for (const SwJsonValue& mv : msgs) {
                        if (!mv.isObject()) {
                            continue;
                        }
                        const SwJsonObject mo = mv.toObject();
                        Message m;
                        m.messageId = toSwString_(mo["messageId"]);
                        m.fromUserId = toSwString_(mo["fromUserId"]);
                        m.status = toSwString_(mo["status"]);
                        m.text = toSwString_(mo["text"]);
                        m.role = toSwString_(mo["role"]);
                        m.kind = toSwString_(mo["kind"]);
                        m.payload = toSwString_(mo["payload"]);
                        m.meta = toSwString_(mo["meta"]);
                        m.reaction = toSwString_(mo["reaction"]);
                        c.messages.append(m);
                    }
                }

                m_conversations.append(c);
            }
        }

        return !m_conversations.isEmpty();
    }

    // v2+: full store.
    const SwJsonValue userV = root["user"];
    if (userV.isObject()) {
        const SwJsonObject uo = userV.toObject();
        m_user.loggedIn = uo["loggedIn"].toBool();
        m_user.displayName = toSwString_(uo["displayName"]);
        m_user.phone = toSwString_(uo["phone"]);
        m_user.initial = toSwString_(uo["initial"]);
        m_user.avatarColor = toColor_(uo["avatarColor"], m_user.avatarColor);
    }

    const SwJsonValue contactsV = root["contacts"];
    if (contactsV.isArray()) {
        const SwJsonArray arr = contactsV.toArray();
        for (const SwJsonValue& item : arr) {
            if (!item.isObject()) {
                continue;
            }
            const SwJsonObject co = item.toObject();
            Contact c;
            c.id = toSwString_(co["id"]);
            c.displayName = toSwString_(co["displayName"]);
            c.phone = toSwString_(co["phone"]);
            c.status = toSwString_(co["status"]);
            c.initial = toSwString_(co["initial"]);
            c.avatarColor = toColor_(co["avatarColor"], SwColor{100, 116, 139});
            if (!c.id.isEmpty()) {
                m_contacts.append(c);
            }
        }
    }

    const SwJsonValue convsV = root["conversations"];
    if (convsV.isArray()) {
        const SwJsonArray arr = convsV.toArray();
        for (const SwJsonValue& item : arr) {
            if (!item.isObject()) {
                continue;
            }
            const SwJsonObject co = item.toObject();

            Conversation c;
            c.id = toSwString_(co["id"]);
            c.type = toSwString_(co["type"]);
            c.title = toSwString_(co["title"]);
            c.status = toSwString_(co["status"]);
            c.avatarInitial = toSwString_(co["avatarInitial"]);
            c.avatarColor = toColor_(co["avatarColor"], SwColor{100, 116, 139});
            c.participantIds = toStringList_(co["participantIds"]);
            c.favorite = co["favorite"].toBool();
            c.unreadCount = co["unreadCount"].toInt();

            const SwJsonValue msgsV = co["messages"];
            if (msgsV.isArray()) {
                const SwJsonArray msgs = msgsV.toArray();
                for (const SwJsonValue& mv : msgs) {
                    if (!mv.isObject()) {
                        continue;
                    }
                    const SwJsonObject mo = mv.toObject();
                    Message m;
                    m.messageId = toSwString_(mo["messageId"]);
                    m.fromUserId = toSwString_(mo["fromUserId"]);
                    m.status = toSwString_(mo["status"]);
                    m.text = toSwString_(mo["text"]);
                    m.role = toSwString_(mo["role"]);
                    m.kind = toSwString_(mo["kind"]);
                    m.payload = toSwString_(mo["payload"]);
                    m.meta = toSwString_(mo["meta"]);
                    m.reaction = toSwString_(mo["reaction"]);
                    c.messages.append(m);
                }
            }

            if (!c.id.isEmpty()) {
                if (c.type.isEmpty()) {
                    c.type = c.participantIds.size() > 1 ? "group" : "direct";
                }
                m_conversations.append(c);
            }
        }
    }

    const SwJsonValue uiV = root["ui"];
    if (uiV.isObject()) {
        const SwJsonObject uio = uiV.toObject();
        m_ui.selectedConversationId = toSwString_(uio["selectedConversationId"]);

        const SwJsonValue draftsV = uio["drafts"];
        if (draftsV.isObject()) {
            const SwJsonObject drafts = draftsV.toObject();
            for (auto it = drafts.begin(); it != drafts.end(); ++it) {
                const SwString key = it.key();
                if (key.isEmpty()) {
                    continue;
                }
                const SwString val = toSwString_(it.value());
                if (!val.isEmpty()) {
                    m_ui.draftsByConversationId.insert(key, val);
                }
            }
        }
    } else {
        // Backward compat if ui was missing but we still had the old root key.
        m_ui.selectedConversationId = toSwString_(root["selectedConversationId"]);
    }

    return !m_conversations.isEmpty();
}

bool WaLocalStore::selectedIdExists_() const {
    if (m_ui.selectedConversationId.isEmpty()) {
        return false;
    }
    for (int i = 0; i < m_conversations.size(); ++i) {
        if (m_conversations[i].id == m_ui.selectedConversationId) {
            return true;
        }
    }
    return false;
}

void WaLocalStore::seedDefault_() {
    clear_();

    // Local user is logged-out by default. Once logged in, we persist it in the store.
    m_user.loggedIn = false;
    m_user.displayName = SwString();
    m_user.phone = SwString();
    m_user.initial = SwString();
    m_user.avatarColor = SwColor{0, 168, 132};

    // Contacts (ids match conversation ids for simplicity).
    m_contacts.clear();
    m_contacts.append(Contact{"c1", "Maxence Merio", "", "en ligne", "M", SwColor{82, 196, 26}});
    m_contacts.append(Contact{"c2", "BenJ", "", "", "B", SwColor{75, 85, 99}});
    m_contacts.append(Contact{"c3", "Edith Pont", "", "", "E", SwColor{14, 116, 144}});
    m_contacts.append(Contact{"c4", "Gilles Spherea", "", "", "G", SwColor{56, 189, 248}});
    m_contacts.append(Contact{"c5", "Support", "", "", "S", SwColor{244, 63, 94}});
    m_contacts.append(Contact{"c6", "Remy Thomas Merio", "", "", "R", SwColor{2, 132, 199}});
    m_contacts.append(Contact{"c7", "+33 6 30 81 42 82", "", "", "+", SwColor{156, 163, 175}});
    m_contacts.append(Contact{"c8", "Simon Mouillé Merio", "", "", "S", SwColor{100, 116, 139}});
    m_contacts.append(Contact{"c9", "Flytech~Milvus7 Trst", "", "", "F", SwColor{31, 41, 55}});
    m_contacts.append(Contact{"c10", "Cedric Bro", "", "", "C", SwColor{17, 24, 39}});

    // Conversations.
    m_conversations.clear();
    m_conversations.append(Conversation{"c1", "direct", "", "", "", SwColor{100, 116, 139}, SwList<SwString>{"c1"}, 0, {}});
    m_conversations.append(Conversation{"c2", "direct", "", "", "", SwColor{100, 116, 139}, SwList<SwString>{"c2"}, 0, {}});
    m_conversations.append(Conversation{"c3", "direct", "", "", "", SwColor{100, 116, 139}, SwList<SwString>{"c3"}, 0, {}});
    m_conversations.append(Conversation{"c4", "direct", "", "", "", SwColor{100, 116, 139}, SwList<SwString>{"c4"}, 0, {}});
    m_conversations.append(Conversation{"c5", "direct", "", "", "", SwColor{100, 116, 139}, SwList<SwString>{"c5"}, 0, {}});
    m_conversations.append(Conversation{"c6", "direct", "", "", "", SwColor{100, 116, 139}, SwList<SwString>{"c6"}, 0, {}});
    m_conversations.append(Conversation{"c7", "direct", "", "", "", SwColor{100, 116, 139}, SwList<SwString>{"c7"}, 0, {}});
    m_conversations.append(Conversation{"c8", "direct", "", "", "", SwColor{100, 116, 139}, SwList<SwString>{"c8"}, 0, {}});
    m_conversations.append(Conversation{"c9", "direct", "", "", "", SwColor{100, 116, 139}, SwList<SwString>{"c9"}, 0, {}});
    m_conversations.append(Conversation{"c10", "direct", "", "", "", SwColor{100, 116, 139}, SwList<SwString>{"c10"}, 0, {}});

    {
        SwList<SwString> participants;
        participants.append("c1");
        participants.append("c6");
        Conversation c;
        c.id = "c11";
        c.type = "group";
        c.title = "ONeill family 🧑‍👩‍👦";
        c.status = "Groupe";
        c.avatarInitial = "O";
        c.avatarColor = SwColor{2, 132, 199};
        c.participantIds = participants;
        c.unreadCount = 0;
        m_conversations.append(c);
    }

    // Thread seed for c1 (matches the existing screenshot).
    appendMessage("c1", "out", "non non", "10:48");
    appendMessage("c1", "out", "je te montre en video des que c'est good", "10:49");
    appendMessage("c1",
                  "in",
                  "Par ce que moi typiquement des qu'il a fini sa tache je lui dit : continue le MVP... Mais je pourrais tres bien faire en sorte qu'a chaque fois qu'il finisse une feature sa le relance automatiquement. Et tu laisse tourner un PC qui build des SaaS",
                  "Modifié à 10:50",
                  "",
                  "",
                  "❤️");
    appendMessage("c1", "out", "Et oui c'est du vibe code qui vibe code", "11:22");
    appendMessage("c1", "out", "Il faudrait arriver a le ping", "11:22");
    appendMessage("c1", "out", "", "13:13", "image");
    appendMessage("c1", "out", "ca va Chatty a de quoi faire tout l'apres midi", "13:14");
    appendMessage("c1", "in", "Oh ptn enorme 😆 😆", "14:24");
    appendMessage("c1", "in", "Excellent", "14:25");

    // Lightweight seeds for others.
    appendMessage("c2", "in", "je te dis ca des que j'ai vu avec elle", "12:14");
    appendMessage("c3", "in", "A reagit a un message", "Hier");
    appendMessage("c4", "in", "Non je pense avec Florian...", "Hier");
    appendMessage("c5", "in", "Ticket #1421 up", "Hier");
    appendMessage("c6", "in", "Merci !", "jeudi");
    appendMessage("c9", "in", "Vous avez ajouté Simon Mouillé Merio", "Hier");
    appendMessage("c10", "in", "Merci Joyeux Noël 😅 toi aussi Bro", "Hier");

    if (Conversation* c = conversationById("c5")) {
        c->favorite = true;
        c->unreadCount = 3;
    }
    if (Conversation* c = conversationById("c1")) {
        c->favorite = true;
    }

    // UI state.
    m_ui.selectedConversationId = "c1";
    markRead("c1");
}
