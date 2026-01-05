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
 * fireBD - Firebase RTDB low-level types for chat-like use-cases.
 *
 * This module is intentionally lightweight and uses Firebase Realtime Database REST (.json) so it
 * works without the official Firebase SDK.
 **************************************************************************************************/

#include "SwString.h"

#include <cstdint>

enum class FireBDMessageStatus {
    None = 0,
    Sent,
    Delivered,
    Read
};

inline SwString fireBdStatusToString(FireBDMessageStatus status) {
    switch (status) {
    case FireBDMessageStatus::Sent:
        return "sent";
    case FireBDMessageStatus::Delivered:
        return "delivered";
    case FireBDMessageStatus::Read:
        return "read";
    default:
        return "none";
    }
}

inline FireBDMessageStatus fireBdStatusFromString(const SwString& status) {
    const SwString s = status.toLower();
    if (s == "sent") return FireBDMessageStatus::Sent;
    if (s == "delivered") return FireBDMessageStatus::Delivered;
    if (s == "read") return FireBDMessageStatus::Read;
    return FireBDMessageStatus::None;
}

struct FireBDMessage {
    SwString messageId;
    SwString conversationId;

    SwString fromUserId;
    SwString toUserId;

    SwString text;
    SwString kind;    // "text" | "image" | "video" | "file" ...
    SwString payload; // optional
    SwString meta;    // optional display meta (time string)

    std::int64_t sentAtMs{0};
};

struct FireBDStatusEvent {
    SwString eventId; // key in Firebase (unique per event)
    SwString messageId;
    SwString conversationId;
    SwString fromUserId;
    SwString toUserId;
    FireBDMessageStatus status{FireBDMessageStatus::None};
    std::int64_t atMs{0};
};

