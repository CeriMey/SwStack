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

/**
 * @file src/fireBD/FireBDTypes.h
 * @ingroup firebd
 * @brief Declares the public interface exposed by FireBDTypes in the FireBD service layer.
 *
 * This header belongs to the FireBD service layer. It declares application-facing clients,
 * service types, and data models used to communicate with the FireBD backend.
 *
 * Within that layer, this file focuses on the fire bd types interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are FireBDMessageStatus, FireBDMessage, and
 * FireBDStatusEvent.
 *
 * Type-oriented declarations here establish shared vocabulary for the surrounding subsystem so
 * multiple components can exchange data and configuration without ad-hoc conventions.
 *
 * The contracts in this area mainly describe request and response shapes, client composition, and
 * higher-level service boundaries.
 *
 */


/***************************************************************************************************
 * fireBD - Firebase RTDB low-level types for chat-like use-cases.
 *
 * This module is intentionally lightweight and uses Firebase Realtime Database REST (.json) so it
 * works without the official Firebase SDK.
 **************************************************************************************************/

#include "SwString.h"

#include <cstdint>

enum class FireBDMessageStatus {
    Unset = 0,  // was "None" — renamed to avoid conflict with X11 "#define None 0L"
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
    return FireBDMessageStatus::Unset;
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
    FireBDMessageStatus status{FireBDMessageStatus::Unset};
    std::int64_t atMs{0};
};
