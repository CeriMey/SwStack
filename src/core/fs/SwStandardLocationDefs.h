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

// Shared enums defining standard locations and path transformations.
enum class SwStandardPathType {
    Windows,
    WindowsLong,
    Unix,
    Mixed,
    Undefined
};

enum class SwStandardLocationId {
    Desktop,
    Documents,
    Downloads,
    Music,
    Pictures,
    Videos,
    Home,
    Temp,
    AppData,
    LocalAppData,
    RoamingAppData,
    Cache,
    Config,
    StartMenu,
    Startup,
    Recent,
    SendTo,
    Favorites,
    PublicDesktop,
    PublicDocuments,
    PublicDownloads,
    PublicPictures,
    PublicMusic,
    PublicVideos,
    ProgramFiles,
    ProgramFilesX86,
    ProgramFilesCommon,
    ProgramFilesCommonX86,
    System,
    SystemX86,
    Windows,
    AdminTools,
    CommonAdminTools,
    Network,
    Public,
    PublicLibraries,
    PublicRingtones,
    SavedGames,
    SavedPictures,
    SavedVideos,
    CameraRoll,
    Screenshots,
    Playlists,
    CommonStartup,
    CommonPrograms,
    CommonStartMenu,
    InternetCache,
    Cookies,
    History,
    ApplicationShortcuts
};
