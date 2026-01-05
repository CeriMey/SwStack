/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#pragma once

/***************************************************************************************************
 * fireBD - Firebase RTDB chat-oriented service.
 *
 * Data layout (Realtime Database):
 * - inbox/<toUserId>/<messageId> = FireBDMessage (object)
 * - statusQueue/<toUserId>/<eventId> = FireBDStatusEvent (object)
 *
 * Polling:
 * - Periodically GET inbox/<me>.json, emit messages, then PATCH inbox/<me>.json with nulls to delete.
 * - Periodically GET statusQueue/<me>.json, emit events, then PATCH statusQueue/<me>.json with nulls to delete.
 **************************************************************************************************/

#include "fireBD/FireBDHttpClient.h"
#include "fireBD/FireBDTypes.h"

#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwJsonValue.h"
#include "SwList.h"
#include "SwTimer.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <functional>
#include <string>

static constexpr const char* kSwLogCategory_FireBDChatService = "sw.firebd.chat";

class FireBDChatService : public SwObject {
    SW_OBJECT(FireBDChatService, SwObject)

public:
    explicit FireBDChatService(SwObject* parent = nullptr)
        : SwObject(parent) {}

    void setDatabaseUrl(SwString url) {
        url = url.trimmed();
        while (url.endsWith("/")) {
            url.chop(1);
        }
        m_baseUrl = url;
    }

    SwString databaseUrl() const { return m_baseUrl; }

    void setAuthToken(const SwString& token) { m_authToken = token.trimmed(); }
    SwString authToken() const { return m_authToken; }

    void setUserId(const SwString& userId) { m_userId = userId.trimmed(); }
    SwString userId() const { return m_userId; }

    void setPollIntervalMs(int ms) { m_pollIntervalMs = std::max(250, ms); }
    int pollIntervalMs() const { return m_pollIntervalMs; }

    bool start() {
        if (m_running) {
            return true;
        }
        if (m_baseUrl.isEmpty() || m_userId.isEmpty()) {
            return false;
        }
        if (!m_timer) {
            m_timer = new SwTimer(this);
            m_timer->setInterval(m_pollIntervalMs);
            SwObject::connect(m_timer, &SwTimer::timeout, this, [this]() { pollNow(); });
        } else {
            m_timer->setInterval(m_pollIntervalMs);
        }
        m_running = true;
        m_timer->start();
        pollNow();
        return true;
    }

    void stop() {
        m_running = false;
        m_pollInFlight = false;
        if (m_timer) {
            m_timer->stop();
        }
    }

    bool isRunning() const { return m_running; }

    // Returns the messageId (generated if empty). Completion is emitted via outgoingMessageResult.
    SwString sendMessage(const SwString& toUserId,
                         const SwString& conversationId,
                         const SwString& text,
                         const SwString& kind = SwString("text"),
                         const SwString& payload = SwString(),
                         const SwString& meta = SwString(),
                         SwString messageId = SwString()) {
        const SwString to = toUserId.trimmed();
        if (m_baseUrl.isEmpty() || m_userId.isEmpty() || to.isEmpty()) {
            outgoingMessageResult(SwString(), false);
            return SwString();
        }

        FireBDMessage msg;
        msg.messageId = messageId.trimmed().isEmpty() ? makeMessageId_() : messageId.trimmed();
        msg.conversationId = conversationId;
        msg.fromUserId = m_userId;
        msg.toUserId = to;
        msg.text = text;
        msg.kind = kind.isEmpty() ? SwString("text") : kind;
        msg.payload = payload;
        msg.meta = meta;
        msg.sentAtMs = nowMs_();

        const SwString url = buildUrl_(inboxItemPath_(msg.toUserId, msg.messageId));
        const SwByteArray body = SwByteArray(encodeMessageJson_(msg).toStdString());

        auto* http = new FireBDHttpClient(this);
        SwObject::connect(http, &FireBDHttpClient::finished, this, [this, http, mid = msg.messageId](const SwByteArray&) {
            const bool ok = (http->statusCode() >= 200 && http->statusCode() < 300);
            http->deleteLater();
            outgoingMessageResult(mid, ok);
        });
        SwObject::connect(http, &FireBDHttpClient::errorOccurred, this, [this, http, mid = msg.messageId](int) {
            http->deleteLater();
            outgoingMessageResult(mid, false);
        });

        http->put(url, body, "application/json");
        return msg.messageId;
    }

    void sendMessageStatus(const SwString& originalSenderId,
                           const SwString& conversationId,
                           const SwString& messageId,
                           FireBDMessageStatus status) {
        const SwString sender = originalSenderId.trimmed();
        const SwString mid = messageId.trimmed();
        if (m_baseUrl.isEmpty() || m_userId.isEmpty() || sender.isEmpty() || mid.isEmpty()) {
            return;
        }

        FireBDStatusEvent ev;
        ev.messageId = mid;
        ev.conversationId = conversationId;
        ev.fromUserId = m_userId;
        ev.toUserId = sender;
        ev.status = status;
        ev.atMs = nowMs_();
        ev.eventId = makeStatusEventId_(mid, status);

        const SwString url = buildUrl_(statusQueueItemPath_(sender, ev.eventId));
        const SwByteArray body = SwByteArray(encodeStatusEventJson_(ev).toStdString());

        auto* http = new FireBDHttpClient(this);
        SwObject::connect(http, &FireBDHttpClient::finished, this, [http](const SwByteArray&) { http->deleteLater(); });
        SwObject::connect(http, &FireBDHttpClient::errorOccurred, this, [http](int) { http->deleteLater(); });
        http->put(url, body, "application/json");
    }

    // Force a poll cycle (safe to call even if not started).
    void pollNow() {
        if (!m_running) {
            return;
        }
        if (m_pollInFlight) {
            return;
        }
        if (m_baseUrl.isEmpty() || m_userId.isEmpty()) {
            return;
        }
        m_pollInFlight = true;
        pollInbox_();
    }

signals:
    DECLARE_SIGNAL(incomingMessages, const SwList<FireBDMessage>&)
    DECLARE_SIGNAL(statusEvents, const SwList<FireBDStatusEvent>&)
    DECLARE_SIGNAL(outgoingMessageResult, const SwString&, bool)

private:
    static std::int64_t nowMs_() {
        const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        return static_cast<std::int64_t>(now.time_since_epoch().count());
    }

    SwString inboxItemPath_(const SwString& toUserId, const SwString& messageId) const {
        return SwString("inbox/") + urlEncode_(toUserId) + "/" + urlEncode_(messageId) + ".json";
    }

    SwString inboxRootPath_() const {
        return SwString("inbox/") + urlEncode_(m_userId) + ".json";
    }

    SwString statusQueueRootPath_() const {
        return SwString("statusQueue/") + urlEncode_(m_userId) + ".json";
    }

    SwString statusQueueItemPath_(const SwString& toUserId, const SwString& eventId) const {
        return SwString("statusQueue/") + urlEncode_(toUserId) + "/" + urlEncode_(eventId) + ".json";
    }

    SwString buildUrl_(const SwString& relativePathWithJson) const {
        SwString url = m_baseUrl;
        if (!relativePathWithJson.isEmpty() && !relativePathWithJson.startsWith("/")) {
            url += "/";
        }
        url += relativePathWithJson;
        if (!m_authToken.isEmpty()) {
            const SwString sep = url.contains("?") ? "&" : "?";
            url += sep + "auth=" + urlEncode_(m_authToken);
        }
        return url;
    }

    static SwString urlEncode_(const SwString& s) {
        const std::string in = s.toStdString();
        std::string out;
        out.reserve(in.size() * 3);
        static const char* hex = "0123456789ABCDEF";
        for (unsigned char c : in) {
            const bool unreserved = (std::isalnum(c) != 0) || c == '-' || c == '_' || c == '.' || c == '~';
            if (unreserved) {
                out.push_back(static_cast<char>(c));
            } else {
                out.push_back('%');
                out.push_back(hex[(c >> 4) & 0xF]);
                out.push_back(hex[c & 0xF]);
            }
        }
        return SwString(out);
    }

    static SwString makeMessageId_() {
        static std::int64_t counter = 0;
        ++counter;
        return SwString("m_") + SwString(std::to_string(nowMs_())) + "_" + SwString(std::to_string(counter));
    }

    static SwString makeStatusEventId_(const SwString& messageId, FireBDMessageStatus status) {
        return messageId + "_" + fireBdStatusToString(status);
    }

    static SwString encodeMessageJson_(const FireBDMessage& msg) {
        SwJsonObject o;
        o["messageId"] = SwJsonValue(msg.messageId.toStdString());
        o["conversationId"] = SwJsonValue(msg.conversationId.toStdString());
        o["fromUserId"] = SwJsonValue(msg.fromUserId.toStdString());
        o["toUserId"] = SwJsonValue(msg.toUserId.toStdString());
        o["text"] = SwJsonValue(msg.text.toStdString());
        o["kind"] = SwJsonValue(msg.kind.toStdString());
        o["payload"] = SwJsonValue(msg.payload.toStdString());
        o["meta"] = SwJsonValue(msg.meta.toStdString());
        o["sentAtMs"] = SwJsonValue(static_cast<long long>(msg.sentAtMs));
        SwJsonDocument doc(o);
        return doc.toJson(SwJsonDocument::JsonFormat::Compact);
    }

    static SwString encodeStatusEventJson_(const FireBDStatusEvent& ev) {
        SwJsonObject o;
        o["eventId"] = SwJsonValue(ev.eventId.toStdString());
        o["messageId"] = SwJsonValue(ev.messageId.toStdString());
        o["conversationId"] = SwJsonValue(ev.conversationId.toStdString());
        o["fromUserId"] = SwJsonValue(ev.fromUserId.toStdString());
        o["toUserId"] = SwJsonValue(ev.toUserId.toStdString());
        o["status"] = SwJsonValue(fireBdStatusToString(ev.status).toStdString());
        o["atMs"] = SwJsonValue(static_cast<long long>(ev.atMs));
        SwJsonDocument doc(o);
        return doc.toJson(SwJsonDocument::JsonFormat::Compact);
    }

    static SwString jsonString_(const SwJsonValue& v) {
        if (!v.isString()) {
            return SwString();
        }
        return SwString(v.toString());
    }

    static std::int64_t jsonInt64_(const SwJsonValue& v, std::int64_t fallback = 0) {
        if (v.isInt()) return static_cast<std::int64_t>(v.toLongLong());
        if (v.isDouble()) return static_cast<std::int64_t>(v.toDouble());
        return fallback;
    }

    static FireBDMessage decodeMessage_(const SwString& key, const SwJsonValue& v) {
        FireBDMessage msg;
        msg.messageId = key;
        if (!v.isObject() || !v.toObject()) {
            return msg;
        }
        const SwJsonObject& o = *v.toObject();
        const SwString mid = jsonString_(o.value("messageId"));
        if (!mid.isEmpty()) {
            msg.messageId = mid;
        }
        msg.conversationId = jsonString_(o.value("conversationId"));
        msg.fromUserId = jsonString_(o.value("fromUserId"));
        msg.toUserId = jsonString_(o.value("toUserId"));
        msg.text = jsonString_(o.value("text"));
        msg.kind = jsonString_(o.value("kind"));
        msg.payload = jsonString_(o.value("payload"));
        msg.meta = jsonString_(o.value("meta"));
        msg.sentAtMs = jsonInt64_(o.value("sentAtMs"), 0);
        return msg;
    }

    static FireBDStatusEvent decodeStatusEvent_(const SwString& key, const SwJsonValue& v) {
        FireBDStatusEvent ev;
        ev.eventId = key;
        if (!v.isObject() || !v.toObject()) {
            return ev;
        }
        const SwJsonObject& o = *v.toObject();
        const SwString eventId = jsonString_(o.value("eventId"));
        if (!eventId.isEmpty()) {
            ev.eventId = eventId;
        }
        ev.messageId = jsonString_(o.value("messageId"));
        ev.conversationId = jsonString_(o.value("conversationId"));
        ev.fromUserId = jsonString_(o.value("fromUserId"));
        ev.toUserId = jsonString_(o.value("toUserId"));
        ev.status = fireBdStatusFromString(jsonString_(o.value("status")));
        ev.atMs = jsonInt64_(o.value("atMs"), 0);
        return ev;
    }

    void pollInbox_() {
        const SwString url = buildUrl_(inboxRootPath_());
        auto* http = new FireBDHttpClient(this);

        SwObject::connect(http, &FireBDHttpClient::finished, this, [this, http](const SwByteArray& body) {
            const bool ok = (http->statusCode() >= 200 && http->statusCode() < 300);
            const SwString json = SwString(body);
            http->deleteLater();
            if (!m_running) {
                m_pollInFlight = false;
                return;
            }
            if (!ok) {
                swCWarning(kSwLogCategory_FireBDChatService) << "[pollInbox] HTTP " << http->statusCode() << " " << http->reasonPhrase();
                m_pollInFlight = false;
                return;
            }

            SwList<SwString> keysToDelete;
            SwList<FireBDMessage> msgs = parseMessages_(json, keysToDelete);
            if (!msgs.isEmpty()) {
                incomingMessages(msgs);
                deleteKeys_(inboxRootPath_(), keysToDelete, [this, msgs]() {
                    if (!m_running) {
                        m_pollInFlight = false;
                        return;
                    }
                    for (int i = 0; i < msgs.size(); ++i) {
                        const FireBDMessage& m = msgs[i];
                        if (!m.fromUserId.isEmpty() && !m.messageId.isEmpty()) {
                            sendMessageStatus(m.fromUserId, m.conversationId, m.messageId, FireBDMessageStatus::Delivered);
                        }
                    }
                    pollStatusQueue_();
                });
                return;
            }

            pollStatusQueue_();
        });

        SwObject::connect(http, &FireBDHttpClient::errorOccurred, this, [this, http](int) {
            http->deleteLater();
            m_pollInFlight = false;
        });

        http->get(url);
    }

    void pollStatusQueue_() {
        const SwString url = buildUrl_(statusQueueRootPath_());
        auto* http = new FireBDHttpClient(this);

        SwObject::connect(http, &FireBDHttpClient::finished, this, [this, http](const SwByteArray& body) {
            const bool ok = (http->statusCode() >= 200 && http->statusCode() < 300);
            const SwString json = SwString(body);
            http->deleteLater();
            if (!m_running) {
                m_pollInFlight = false;
                return;
            }
            if (!ok) {
                swCWarning(kSwLogCategory_FireBDChatService) << "[pollStatusQueue] HTTP " << http->statusCode() << " " << http->reasonPhrase();
                m_pollInFlight = false;
                return;
            }

            SwList<SwString> keysToDelete;
            SwList<FireBDStatusEvent> evs = parseStatusEvents_(json, keysToDelete);
            if (!evs.isEmpty()) {
                statusEvents(evs);
                deleteKeys_(statusQueueRootPath_(), keysToDelete, [this]() { m_pollInFlight = false; });
                return;
            }

            m_pollInFlight = false;
        });

        SwObject::connect(http, &FireBDHttpClient::errorOccurred, this, [this, http](int) {
            http->deleteLater();
            m_pollInFlight = false;
        });

        http->get(url);
    }

    SwList<FireBDMessage> parseMessages_(const SwString& json, SwList<SwString>& outKeys) const {
        SwList<FireBDMessage> out;
        outKeys.clear();

        SwString err;
        SwJsonDocument doc = SwJsonDocument::fromJson(json.toStdString(), err);
        SwJsonValue root = doc.toJsonValue();
        if (root.isNull()) {
            return out;
        }
        if (!root.isObject() || !root.toObject()) {
            return out;
        }

        const SwJsonObject& obj = *root.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const SwString key = it.key();
            const SwJsonValue& v = it.value();
            out.append(decodeMessage_(key, v));
            outKeys.append(key);
        }
        return out;
    }

    SwList<FireBDStatusEvent> parseStatusEvents_(const SwString& json, SwList<SwString>& outKeys) const {
        SwList<FireBDStatusEvent> out;
        outKeys.clear();

        SwString err;
        SwJsonDocument doc = SwJsonDocument::fromJson(json.toStdString(), err);
        SwJsonValue root = doc.toJsonValue();
        if (root.isNull()) {
            return out;
        }
        if (!root.isObject() || !root.toObject()) {
            return out;
        }

        const SwJsonObject& obj = *root.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const SwString key = it.key();
            const SwJsonValue& v = it.value();
            out.append(decodeStatusEvent_(key, v));
            outKeys.append(key);
        }
        return out;
    }

    void deleteKeys_(const SwString& rootPathJson, const SwList<SwString>& keys, std::function<void()> done) {
        if (keys.isEmpty()) {
            if (done) {
                done();
            }
            return;
        }

        SwJsonObject patch;
        for (int i = 0; i < keys.size(); ++i) {
            patch[keys[i].toStdString()] = SwJsonValue(); // null -> delete key
        }
        SwJsonDocument doc(patch);
        const SwString bodyStr = doc.toJson(SwJsonDocument::JsonFormat::Compact);
        const SwByteArray body = SwByteArray(bodyStr.toStdString());

        const SwString url = buildUrl_(rootPathJson);
        auto* http = new FireBDHttpClient(this);

        SwObject::connect(http, &FireBDHttpClient::finished, this, [http, done](const SwByteArray&) {
            http->deleteLater();
            if (done) {
                done();
            }
        });
        SwObject::connect(http, &FireBDHttpClient::errorOccurred, this, [http, done](int) {
            http->deleteLater();
            if (done) {
                done();
            }
        });

        http->patch(url, body, "application/json");
    }

    SwString m_baseUrl;
    SwString m_authToken;
    SwString m_userId;

    int m_pollIntervalMs{1000};
    bool m_running{false};
    bool m_pollInFlight{false};

    SwTimer* m_timer{nullptr};
};
