#pragma once
#include "SwObject.h"
#include "SwString.h"
#include "SwJsonDocument.h"
#include "SwJsonArray.h"
#include "SwJsonObject.h"
#include "SwList.h"
#include "SwDir.h"
#include "platform/SwPlatform.h"
#include "SwDebug.h"
#include <fstream>

struct MapEntry {
    SwString name;
    SwString url;
};

class MapDatabase {
public:
    static MapDatabase* instance() {
        static MapDatabase db;
        return &db;
    }

    SwString storageFile() const { return m_storageFile; }

    SwList<MapEntry> all() const { return m_entries; }

    bool addOrUpdate(const SwString& name, const SwString& url) {
        if (name.isEmpty() || url.isEmpty()) return false;
        SwString normalized = name.toLower();
        bool updated = false;
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].name == normalized) {
                m_entries[i].url = url;
                updated = true;
                break;
            }
        }
        if (!updated) {
            MapEntry e{ normalized, url };
            m_entries.append(e);
        }
        ensureCacheFolder(normalized);
        return save();
    }

    bool remove(const SwString& name) {
        SwString normalized = name.toLower();
        SwList<MapEntry> next;
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].name != normalized) next.append(m_entries[i]);
        }
        bool changed = next.size() != m_entries.size();
        m_entries = next;
        return changed ? save() : true;
    }

    bool contains(const SwString& name) const {
        SwString normalized = name.toLower();
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].name == normalized) return true;
        }
        return false;
    }

    SwString urlFor(const SwString& name) const {
        SwString normalized = name.toLower();
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].name == normalized) return m_entries[i].url;
        }
        return SwString();
    }

private:
    MapDatabase() {
        m_storageFile = SwString("maps.json");
        load();
        ensureDefaults();
    }

    void ensureDefaults() {
        auto ensureOne = [this](const SwString& name, const SwString& url) {
            SwString normalized = name.toLower();
            for (size_t i = 0; i < m_entries.size(); ++i) {
                if (m_entries[i].name == normalized) {
                    if (m_entries[i].url != url) {
                        m_entries[i].url = url;
                        save();
                    }
                    ensureCacheFolder(normalized);
                    return;
                }
            }
            m_entries.append(MapEntry{ normalized, url });
            ensureCacheFolder(normalized);
            save();
        };

        ensureOne(SwString("standard"), SwString("https://tile.openstreetmap.org"));
        ensureOne(SwString("elevation"), SwString("https://api.mapbox.com/v4/mapbox.terrain-rgb/%1/%2/%3.pngraw?access_token=pk.eyJ1IjoiZXltZXJpYyIsImEiOiJjbWd0a24ydHkwNDdpMmxzOG5scTJxcWVoIn0.wwYIKczafPghDK_tc7eYXQ"));
    }

    bool load() {
        std::ifstream in(m_storageFile.toStdString().c_str(), std::ios::binary);
        if (!in) return false;
        std::ostringstream buf;
        buf << in.rdbuf();
        SwString err;
        SwJsonDocument doc = SwJsonDocument::fromJson(buf.str(), err);
        if (!err.isEmpty()) {
            swError() << "[MapDatabase] failed to parse maps.json: " << err.toStdString();
            return false;
        }
        SwJsonArray arr = doc.array();
        SwList<MapEntry> tmp;
        bool normalizedData = false;
        for (size_t i = 0; i < arr.size(); ++i) {
            SwJsonValue val = arr[i];
            if (!val.isObject()) continue;
            SwJsonObject obj = *val.toObject();
            SwString name = obj.contains("name") ? SwString(obj["name"].toString()) : SwString();
            SwString url = obj.contains("url") ? SwString(obj["url"].toString()) : SwString();
            if (name.isEmpty() || url.isEmpty()) continue;

            SwString normalized = name.toLower();
            if (normalized != name) {
                normalizedData = true;
            }

            bool merged = false;
            for (size_t j = 0; j < tmp.size(); ++j) {
                if (tmp[j].name == normalized) {
                    tmp[j].url = url;
                    if (tmp[j].name != name) {
                        normalizedData = true;
                    }
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                tmp.append(MapEntry{ normalized, url });
            }
        }
        if (!tmp.isEmpty()) {
            bool changed = (m_entries.size() != tmp.size());
            if (!changed) {
                for (size_t i = 0; i < tmp.size(); ++i) {
                    if (m_entries[i].name != tmp[i].name || m_entries[i].url != tmp[i].url) {
                        changed = true;
                        break;
                    }
                }
            }
            m_entries = tmp;
            for (size_t i = 0; i < m_entries.size(); ++i) {
                ensureCacheFolder(m_entries[i].name);
            }
            if (normalizedData || changed) {
                save();
            }
        }
        return true;
    }

    bool save() {
        SwJsonArray arr;
        for (size_t i = 0; i < m_entries.size(); ++i) {
            SwJsonObject obj;
            obj["name"] = SwJsonValue(m_entries[i].name.toStdString());
            obj["url"] = SwJsonValue(m_entries[i].url.toStdString());
            arr.append(obj);
        }
        SwJsonDocument doc(arr);
        SwString jsonStr = doc.toJson(SwJsonDocument::JsonFormat::Pretty);
        std::string json = jsonStr.toStdString();
        std::ofstream out(m_storageFile.toStdString().c_str(), std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(json.c_str(), static_cast<std::streamsize>(json.size()));
        return out.good();
    }

    void ensureCacheFolder(const SwString& normalized) const {
        if (normalized.isEmpty()) return;
        SwString relative = SwString("maps/") + normalized;
        SwString target = swDirPlatform().absolutePath(relative);
        if (target.isEmpty()) {
            swWarning() << "[MapDatabase] Unable to resolve absolute path for " << relative.toStdString();
            return;
        }
        if (SwDir::exists(target)) {
            return;
        }
        if (!SwDir::mkpathAbsolute(target, false)) {
            // If mkpath failed but directory now exists (race), ignore.
            if (!SwDir::exists(target)) {
                swWarning() << "[MapDatabase] Failed to ensure cache directory " << target.toStdString();
            }
        }
    }

    SwList<MapEntry> m_entries;
    SwString m_storageFile;
};
