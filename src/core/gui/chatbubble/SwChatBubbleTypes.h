#pragma once
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

#include "SwString.h"

class SwImage;

enum class SwChatBubbleRole {
    User,
    Bot
};

enum class SwChatMessageKind {
    Text,
    Image
};

enum class SwChatMessageStatus {
    None,
    Sent,
    Delivered,
    Read
};

struct SwChatBubbleMessage {
    SwString text;
    SwString meta;      // time or meta like "Modifié à 10:50"
    SwString reaction;  // emoji reaction displayed under the bubble

    SwChatBubbleRole role{SwChatBubbleRole::Bot};
    SwChatMessageKind kind{SwChatMessageKind::Text};
    SwChatMessageStatus status{SwChatMessageStatus::None};

    // Optional image for image messages (non-owning).
    const SwImage* image{nullptr};
};

