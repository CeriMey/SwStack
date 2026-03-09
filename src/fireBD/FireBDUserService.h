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
 * @file src/fireBD/FireBDUserService.h
 * @ingroup firebd
 * @brief Declares the public interface exposed by FireBDUserService in the FireBD service layer.
 *
 * This header belongs to the FireBD service layer. It declares application-facing clients,
 * service types, and data models used to communicate with the FireBD backend.
 *
 * Within that layer, this file focuses on the fire bd user service interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are FireBDUser and FireBDUserService.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * The contracts in this area mainly describe request and response shapes, client composition, and
 * higher-level service boundaries.
 *
 */


/***************************************************************************************************
 * fireBD - Firebase RTDB user/account service (demo-friendly).
 *
 * Data layout (Realtime Database):
 * - users/<phone> = { phone, pseudo, firstName, lastName, passSalt, passHash, createdAtMs }
 * - userIndex/pseudo/<pseudoLower> = { phone: "<phone>" }
 *
 * Notes:
 * - This is NOT Firebase Auth. It's a lightweight RTDB-based account layer suitable for demos.
 * - Do not ship a real product like this without proper security rules + real auth.
 **************************************************************************************************/

#include "fireBD/FireBDHttpClient.h"

#include "SwCrypto.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwJsonValue.h"
#include "SwList.h"
#include "SwString.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

static constexpr const char* kSwLogCategory_FireBDUserService = "sw.firebd.user";

struct FireBDUser {
    SwString phone;
    SwString pseudo;
    SwString firstName;
    SwString lastName;
    std::int64_t createdAtMs{0};

    /**
     * @brief Returns the current display Name.
     * @return The current display Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString displayName() const {
        if (!pseudo.trimmed().isEmpty()) {
            return pseudo.trimmed();
        }
        SwString out = firstName.trimmed();
        if (!lastName.trimmed().isEmpty()) {
            if (!out.isEmpty()) out += " ";
            out += lastName.trimmed();
        }
        return out;
    }
};

class FireBDUserService : public SwObject {
    SW_OBJECT(FireBDUserService, SwObject)

public:
    /**
     * @brief Constructs a `FireBDUserService` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit FireBDUserService(SwObject* parent = nullptr)
        : SwObject(parent) {}

    /**
     * @brief Sets the database Url.
     * @param url Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDatabaseUrl(SwString url) {
        url = url.trimmed();
        while (url.endsWith("/")) {
            url.chop(1);
        }
        m_baseUrl = url;
    }
    /**
     * @brief Returns the current database Url.
     * @return The current database Url.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString databaseUrl() const { return m_baseUrl; }

    /**
     * @brief Sets the auth Token.
     * @param m_authToken Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAuthToken(const SwString& token) { m_authToken = token.trimmed(); }
    /**
     * @brief Returns the current auth Token.
     * @return The current auth Token.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString authToken() const { return m_authToken; }

    /**
     * @brief Performs the `signUp` operation.
     * @param firstName Value passed to the method.
     * @param lastName Value passed to the method.
     * @param pseudo Value passed to the method.
     * @param phoneRaw Value passed to the method.
     * @param password Value passed to the method.
     */
    void signUp(const SwString& firstName,
                const SwString& lastName,
                const SwString& pseudo,
                const SwString& phoneRaw,
                const SwString& password) {
        if (m_baseUrl.isEmpty()) {
            signUpFinished(false, FireBDUser{}, "Firebase URL manquante.");
            return;
        }

        const SwString phone = normalizePhone_(phoneRaw);
        const SwString pseudoTrim = pseudo.trimmed();
        if (digitCount_(phone) < 6) {
            signUpFinished(false, FireBDUser{}, "Numéro de téléphone invalide.");
            return;
        }
        if (pseudoTrim.isEmpty()) {
            signUpFinished(false, FireBDUser{}, "Pseudo manquant.");
            return;
        }
        if (password.trimmed().isEmpty()) {
            signUpFinished(false, FireBDUser{}, "Mot de passe manquant.");
            return;
        }

        const SwString pseudoKey = normalizePseudoKey_(pseudoTrim);
        if (!isValidPseudoKey_(pseudoKey)) {
            signUpFinished(false, FireBDUser{}, "Pseudo invalide (lettres/chiffres/_/-).");
            return;
        }
        const SwString pseudoIndexUrl = buildUrl_(SwString("userIndex/pseudo/") + urlEncode_(pseudoKey) + ".json");
        auto* httpPseudo = new FireBDHttpClient(this);
        SwObject::connect(httpPseudo, &FireBDHttpClient::finished, this, [this, httpPseudo, firstName, lastName, pseudoTrim, pseudoKey, phone, password](const SwByteArray& body) {
            const int status = httpPseudo->statusCode();
            const SwString json = SwString(body);
            httpPseudo->deleteLater();

            if (status < 200 || status >= 300) {
                signUpFinished(false, FireBDUser{}, formatFirebaseError_("pseudo", status, json));
                return;
            }
            if (!isNullJson_(json)) {
                signUpFinished(false, FireBDUser{}, "Pseudo déjà pris.");
                return;
            }

            const SwString userUrl = buildUrl_(SwString("users/") + urlEncode_(phone) + ".json");
            auto* httpUserCheck = new FireBDHttpClient(this);
            SwObject::connect(httpUserCheck, &FireBDHttpClient::finished, this, [this, httpUserCheck, firstName, lastName, pseudoTrim, pseudoKey, phone, password](const SwByteArray& body2) {
                const int status2 = httpUserCheck->statusCode();
                const SwString json2 = SwString(body2);
                httpUserCheck->deleteLater();

                if (status2 < 200 || status2 >= 300) {
                    signUpFinished(false, FireBDUser{}, formatFirebaseError_("telephone", status2, json2));
                    return;
                }
                if (!isNullJson_(json2)) {
                    signUpFinished(false, FireBDUser{}, "Numéro de téléphone déjà utilisé.");
                    return;
                }

                FireBDUserRecord rec;
                rec.phone = phone;
                rec.pseudo = pseudoTrim;
                rec.firstName = firstName.trimmed();
                rec.lastName = lastName.trimmed();
                rec.createdAtMs = nowMs_();
                rec.passSalt = makeSalt_();
                rec.passHash = hashPassword_(password, rec.passSalt);

                const SwString userPutUrl = buildUrl_(SwString("users/") + urlEncode_(phone) + ".json");
                const SwByteArray userBody = SwByteArray(encodeUserJson_(rec).toStdString());
                auto* httpUserPut = new FireBDHttpClient(this);
                SwObject::connect(httpUserPut, &FireBDHttpClient::finished, this, [this, httpUserPut, rec, pseudoKey](const SwByteArray&) {
                    const int status3 = httpUserPut->statusCode();
                    httpUserPut->deleteLater();
                    if (status3 < 200 || status3 >= 300) {
                        signUpFinished(false, FireBDUser{}, formatFirebaseError_("create user", status3, SwString()));
                        return;
                    }

                    // Create pseudo index mapping.
                    SwJsonObject idx;
                    idx["phone"] = SwJsonValue(rec.phone.toStdString());
                    SwJsonDocument doc(idx);
                    const SwString idxBodyStr = doc.toJson(SwJsonDocument::JsonFormat::Compact);
                    const SwByteArray idxBody = SwByteArray(idxBodyStr.toStdString());

                    const SwString idxUrl = buildUrl_(SwString("userIndex/pseudo/") + urlEncode_(pseudoKey) + ".json");
                    auto* httpIdxPut = new FireBDHttpClient(this);
                    SwObject::connect(httpIdxPut, &FireBDHttpClient::finished, this, [this, httpIdxPut, rec](const SwByteArray&) {
                        const int status4 = httpIdxPut->statusCode();
                        httpIdxPut->deleteLater();
                        if (status4 < 200 || status4 >= 300) {
                            signUpFinished(false, FireBDUser{}, formatFirebaseError_("index pseudo", status4, SwString()));
                            return;
                        }
                        signUpFinished(true, rec.toUser(), SwString());
                    });
                    SwObject::connect(httpIdxPut, &FireBDHttpClient::errorOccurred, this, [this, httpIdxPut](int) {
                        httpIdxPut->deleteLater();
                        signUpFinished(false, FireBDUser{}, "Erreur réseau (index pseudo).");
                    });
                    httpIdxPut->put(idxUrl, idxBody, "application/json");
                });
                SwObject::connect(httpUserPut, &FireBDHttpClient::errorOccurred, this, [this, httpUserPut](int) {
                    httpUserPut->deleteLater();
                    signUpFinished(false, FireBDUser{}, "Erreur réseau (create user).");
                });
                httpUserPut->put(userPutUrl, userBody, "application/json");
            });
            SwObject::connect(httpUserCheck, &FireBDHttpClient::errorOccurred, this, [this, httpUserCheck](int) {
                httpUserCheck->deleteLater();
                signUpFinished(false, FireBDUser{}, "Erreur réseau (check téléphone).");
            });
            httpUserCheck->get(userUrl);
        });
        SwObject::connect(httpPseudo, &FireBDHttpClient::errorOccurred, this, [this, httpPseudo](int) {
            httpPseudo->deleteLater();
            signUpFinished(false, FireBDUser{}, "Erreur réseau (check pseudo).");
        });
        httpPseudo->get(pseudoIndexUrl);
    }

    /**
     * @brief Performs the `logIn` operation.
     * @param identifier Value passed to the method.
     * @param password Value passed to the method.
     */
    void logIn(const SwString& identifier, const SwString& password) {
        if (m_baseUrl.isEmpty()) {
            loginFinished(false, FireBDUser{}, "Firebase URL manquante.");
            return;
        }
        const SwString id = identifier.trimmed();
        if (id.isEmpty()) {
            loginFinished(false, FireBDUser{}, "Pseudo ou téléphone manquant.");
            return;
        }
        if (password.trimmed().isEmpty()) {
            loginFinished(false, FireBDUser{}, "Mot de passe manquant.");
            return;
        }

        const SwString phoneTry = normalizePhone_(id);
        if (digitCount_(phoneTry) >= 6) {
            getUserByPhoneWithPassword_(phoneTry, password, [this, id, password](bool ok, const FireBDUserRecord& rec, const SwString& err) {
                if (ok) {
                    loginFinished(true, rec.toUser(), SwString());
                    return;
                }
                // Fallback to pseudo lookup if not found.
                if (err == "NOT_FOUND") {
                    lookupByPseudoAndLogin_(id, password);
                    return;
                }
                loginFinished(false, FireBDUser{}, err);
            });
            return;
        }

        lookupByPseudoAndLogin_(id, password);
    }

    /**
     * @brief Performs the `lookupUserByPhone` operation.
     * @param phoneRaw Value passed to the method.
     */
    void lookupUserByPhone(const SwString& phoneRaw) {
        if (m_baseUrl.isEmpty()) {
            lookupFinished(false, FireBDUser{}, "Firebase URL manquante.");
            return;
        }
        const SwString phone = normalizePhone_(phoneRaw);
        if (digitCount_(phone) < 6) {
            lookupFinished(false, FireBDUser{}, "Numéro invalide.");
            return;
        }

        const SwString url = buildUrl_(SwString("users/") + urlEncode_(phone) + ".json");
        auto* http = new FireBDHttpClient(this);
        SwObject::connect(http, &FireBDHttpClient::finished, this, [this, http, phone](const SwByteArray& body) {
            const int status = http->statusCode();
            const SwString json = SwString(body);
            http->deleteLater();
            if (status < 200 || status >= 300) {
                lookupFinished(false, FireBDUser{}, formatFirebaseError_("lookup", status, json));
                return;
            }
            if (isNullJson_(json)) {
                lookupFinished(false, FireBDUser{}, "NOT_FOUND");
                return;
            }
            FireBDUserRecord rec;
            if (!decodeUserJson_(json, rec)) {
                lookupFinished(false, FireBDUser{}, "Réponse Firebase invalide.");
                return;
            }
            if (rec.phone.isEmpty()) {
                rec.phone = phone;
            }
            lookupFinished(true, rec.toUser(), SwString());
        });
        SwObject::connect(http, &FireBDHttpClient::errorOccurred, this, [this, http](int) {
            http->deleteLater();
            lookupFinished(false, FireBDUser{}, "Erreur réseau.");
        });
        http->get(url);
    }

signals:
    DECLARE_SIGNAL(signUpFinished, bool, const FireBDUser&, const SwString&)
    DECLARE_SIGNAL(loginFinished, bool, const FireBDUser&, const SwString&)
    DECLARE_SIGNAL(lookupFinished, bool, const FireBDUser&, const SwString&)

private:
    struct FireBDUserRecord : FireBDUser {
        SwString passSalt;
        SwString passHash;

        /**
         * @brief Returns the current to User.
         * @return The current to User.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        FireBDUser toUser() const {
            FireBDUser u;
            u.phone = phone;
            u.pseudo = pseudo;
            u.firstName = firstName;
            u.lastName = lastName;
            u.createdAtMs = createdAtMs;
            return u;
        }
    };

    static std::int64_t nowMs_() {
        const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        return static_cast<std::int64_t>(now.time_since_epoch().count());
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

    static int digitCount_(const SwString& s) {
        const std::string in = s.toStdString();
        int digits = 0;
        for (unsigned char c : in) {
            if (std::isdigit(c) != 0) {
                ++digits;
            }
        }
        return digits;
    }

    static bool isValidPseudoKey_(const SwString& pseudoKey) {
        const std::string s = pseudoKey.trimmed().toLower().toStdString();
        if (s.empty()) {
            return false;
        }
        for (unsigned char c : s) {
            const bool ok = (std::isalnum(c) != 0) || c == '_' || c == '-';
            if (!ok) {
                return false;
            }
        }
        return true;
    }

    static SwString collapseToOneLine_(SwString s) {
        s.replace("\r", " ");
        s.replace("\n", " ");
        while (s.contains("  ")) {
            s.replace("  ", " ");
        }
        return s.trimmed();
    }

    static SwString extractFirebaseErrorMessage_(SwString body) {
        body = body.trimmed();
        if (body.isEmpty()) {
            return SwString();
        }

        SwString err;
        SwJsonDocument doc = SwJsonDocument::fromJson(body.toStdString(), err);
        SwJsonValue root = doc.toJsonValue();
        if (!root.isObject()) {
            return SwString();
        }

        const SwJsonObject o = root.toObject();
        const SwJsonValue ev = o.value("error");
        if (ev.isString()) {
            return SwString(ev.toString()).trimmed();
        }
        if (ev.isObject()) {
            const SwJsonObject eo = ev.toObject();
            const SwJsonValue mv = eo.value("message");
            if (mv.isString()) {
                return SwString(mv.toString()).trimmed();
            }
        }

        const SwJsonValue mv = o.value("message");
        if (mv.isString()) {
            return SwString(mv.toString()).trimmed();
        }

        return SwString();
    }

    static SwString extractPseudoIndexPhone_(SwString json) {
        json = json.trimmed();
        if (isNullJson_(json)) {
            return SwString();
        }

        // If the endpoint is `.../phone.json`, RTDB returns a JSON string: "0652838331"
        // Keep it as-is (do NOT parse into a number, leading zeros are meaningful for phone keys).
        if (json.startsWith("\"")) {
            if (json.size() >= 2 && json.endsWith("\"")) {
                return json.mid(1, static_cast<int>(json.size() - 2)).trimmed();
            }
            return json.trimmed();
        }

        // Extract {"phone":"..."} from raw JSON (preserves leading zeros).
        const SwString lower = json.toLower();
        int key = lower.indexOf("\"phone\"");
        if (key >= 0) {
            int colon = lower.indexOf(':', static_cast<size_t>(key + 7));
            if (colon >= 0) {
                int pos = colon + 1;
                while (pos < static_cast<int>(lower.size()) && std::isspace(static_cast<unsigned char>(lower[static_cast<size_t>(pos)])) != 0) {
                    ++pos;
                }

                if (pos < static_cast<int>(lower.size()) && lower[static_cast<size_t>(pos)] == '\"') {
                    const int q1 = pos;
                    int q2 = lower.indexOf('\"', static_cast<size_t>(q1 + 1));
                    if (q2 > q1) {
                        return json.mid(q1 + 1, q2 - (q1 + 1)).trimmed();
                    }
                } else {
                    // Unquoted primitive (number/bool/etc).
                    int end = pos;
                    while (end < static_cast<int>(lower.size())) {
                        const char ch = lower[static_cast<size_t>(end)];
                        if (ch == ',' || ch == '}' || ch == ']' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
                            break;
                        }
                        ++end;
                    }
                    if (end > pos) {
                        return json.mid(pos, end - pos).trimmed();
                    }
                }
            }
        }

        // Last resort: grab digits from the payload.
        const SwString digits = normalizePhone_(json);
        return digitCount_(digits) >= 6 ? digits : SwString();
    }

    static SwString formatFirebaseError_(const SwString& context, int status, SwString body) {
        body = body.trimmed();
        const SwString extracted = extractFirebaseErrorMessage_(body);
        SwString msg = extracted.isEmpty() ? body : extracted;
        msg = collapseToOneLine_(msg);
        const SwString lower = msg.toLower();
        if (lower.contains("permission") && lower.contains("denied")) {
            return "Acces Firebase refuse (rules). Mets la RTDB en test mode ou fournis un token.";
        }
        if ((lower.contains("invalid") && lower.contains("token")) || (lower.contains("auth") && lower.contains("token"))) {
            return "Token Firebase invalide ou manquant.";
        }
        if (status == 404 && msg.isEmpty()) {
            return "Base RTDB introuvable (404). Cree Realtime Database et verifie l'URL.";
        }
        if (msg.isEmpty()) {
            return SwString("Erreur Firebase (") + context + "): HTTP " + SwString::number(status);
        }
        if (msg.size() > 200) {
            msg = msg.left(200) + "...";
        }
        return SwString("Erreur Firebase (") + context + "): " + msg;
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

    static SwString normalizePseudoKey_(SwString pseudo) {
        pseudo = pseudo.trimmed().toLower();
        return pseudo;
    }

    static bool isNullJson_(SwString json) {
        json = json.trimmed().toLower();
        return json.isEmpty() || json == "null";
    }

    static SwString makeSalt_() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 255);
        std::vector<unsigned char> bytes;
        bytes.resize(16);
        for (size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = static_cast<unsigned char>(dist(gen));
        }
        return SwString(SwCrypto::base64Encode(bytes));
    }

    static SwString hashPassword_(const SwString& password, const SwString& salt) {
        const std::string input = salt.toStdString() + ":" + password.toStdString();
        const std::vector<unsigned char> bytes = SwCrypto::generateHashSHA256(input);
        return SwString(SwCrypto::base64Encode(bytes));
    }

    static SwString encodeUserJson_(const FireBDUserRecord& rec) {
        SwJsonObject o;
        o["phone"] = SwJsonValue(rec.phone.toStdString());
        o["pseudo"] = SwJsonValue(rec.pseudo.toStdString());
        o["firstName"] = SwJsonValue(rec.firstName.toStdString());
        o["lastName"] = SwJsonValue(rec.lastName.toStdString());
        o["passSalt"] = SwJsonValue(rec.passSalt.toStdString());
        o["passHash"] = SwJsonValue(rec.passHash.toStdString());
        o["createdAtMs"] = SwJsonValue(static_cast<long long>(rec.createdAtMs));
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

    static bool decodeUserJson_(const SwString& json, FireBDUserRecord& out) {
        out = FireBDUserRecord{};

        SwString err;
        SwJsonDocument doc = SwJsonDocument::fromJson(json.toStdString(), err);
        SwJsonValue root = doc.toJsonValue();
        if (root.isNull()) {
            return false;
        }
        if (!root.isObject()) {
            return false;
        }

        const SwJsonObject o = root.toObject();
        out.phone = jsonString_(o.value("phone"));
        out.pseudo = jsonString_(o.value("pseudo"));
        out.firstName = jsonString_(o.value("firstName"));
        out.lastName = jsonString_(o.value("lastName"));
        out.passSalt = jsonString_(o.value("passSalt"));
        out.passHash = jsonString_(o.value("passHash"));
        out.createdAtMs = jsonInt64_(o.value("createdAtMs"), 0);
        return true;
    }

    void lookupByPseudoAndLogin_(const SwString& pseudoOrId, const SwString& password) {
        const SwString pseudoKey = normalizePseudoKey_(pseudoOrId);
        if (!isValidPseudoKey_(pseudoKey)) {
            loginFinished(false, FireBDUser{}, "Pseudo invalide (lettres/chiffres/_/-).");
            return;
        }
        const SwString url = buildUrl_(SwString("userIndex/pseudo/") + urlEncode_(pseudoKey) + ".json");
        auto* http = new FireBDHttpClient(this);

        SwObject::connect(http, &FireBDHttpClient::finished, this, [this, http, pseudoOrId, password](const SwByteArray& body) {
            const int status = http->statusCode();
            const SwString json = SwString(body);
            http->deleteLater();

            if (status < 200 || status >= 300) {
                loginFinished(false, FireBDUser{}, formatFirebaseError_("pseudo", status, json));
                return;
            }
            if (isNullJson_(json)) {
                loginFinished(false, FireBDUser{}, "Compte introuvable.");
                return;
            }

            auto continueWithPhone = [this, password](SwString phoneRaw) {
                const SwString phone = normalizePhone_(phoneRaw);
                if (digitCount_(phone) < 6) {
                    loginFinished(false, FireBDUser{}, "Index pseudo invalide (phone manquant).");
                    return;
                }

                getUserByPhoneWithPassword_(phone, password, [this](bool ok, const FireBDUserRecord& rec, const SwString& err2) {
                    if (ok) {
                        loginFinished(true, rec.toUser(), SwString());
                        return;
                    }
                    if (err2 == "NOT_FOUND") {
                        loginFinished(false, FireBDUser{}, "Compte introuvable.");
                        return;
                    }
                    loginFinished(false, FireBDUser{}, err2);
                });
            };

            const SwString phoneRaw = extractPseudoIndexPhone_(json);
            if (digitCount_(normalizePhone_(phoneRaw)) >= 6) {
                continueWithPhone(phoneRaw);
                return;
            }

            swCError(kSwLogCategory_FireBDUserService) << "Pseudo index parse failed. pseudo=" << pseudoOrId
                                                      << " status=" << status
                                                      << " body=" << collapseToOneLine_(json);

            // Fallback: read `.../userIndex/pseudo/<pseudo>/phone.json` directly (some DB layouts store extra fields).
            const SwString pseudoKey = normalizePseudoKey_(pseudoOrId);
            const SwString url2 = buildUrl_(SwString("userIndex/pseudo/") + urlEncode_(pseudoKey) + "/phone.json");
            auto* http2 = new FireBDHttpClient(this);
            SwObject::connect(http2, &FireBDHttpClient::finished, this, [this, http2, continueWithPhone](const SwByteArray& b2) {
                const int st2 = http2->statusCode();
                const SwString j2 = SwString(b2);
                http2->deleteLater();

                if (st2 < 200 || st2 >= 300) {
                    loginFinished(false, FireBDUser{}, formatFirebaseError_("pseudo phone", st2, j2));
                    return;
                }

                const SwString phone2 = extractPseudoIndexPhone_(j2);
                if (digitCount_(normalizePhone_(phone2)) < 6) {
                    swCError(kSwLogCategory_FireBDUserService) << "Pseudo phone.json parse failed. status=" << st2 << " body="
                                                              << collapseToOneLine_(j2);
                }
                continueWithPhone(phone2);
            });
            SwObject::connect(http2, &FireBDHttpClient::errorOccurred, this, [this, http2](int) {
                http2->deleteLater();
                loginFinished(false, FireBDUser{}, "Erreur rǸseau (pseudo phone).");
            });
            http2->get(url2);
        });

        SwObject::connect(http, &FireBDHttpClient::errorOccurred, this, [this, http](int) {
            http->deleteLater();
            loginFinished(false, FireBDUser{}, "Erreur réseau (pseudo).");
        });

        http->get(url);
    }

    void getUserByPhoneWithPassword_(const SwString& phone,
                                     const SwString& password,
                                     std::function<void(bool, const FireBDUserRecord&, const SwString&)> done) {
        const SwString url = buildUrl_(SwString("users/") + urlEncode_(phone) + ".json");
        auto* http = new FireBDHttpClient(this);

        SwObject::connect(http, &FireBDHttpClient::finished, this, [this, http, phone, password, done](const SwByteArray& body) {
            const int status = http->statusCode();
            const SwString json = SwString(body);
            http->deleteLater();

            if (status < 200 || status >= 300) {
                if (done) done(false, FireBDUserRecord{}, formatFirebaseError_("user", status, json));
                return;
            }
            if (isNullJson_(json)) {
                if (done) done(false, FireBDUserRecord{}, "NOT_FOUND");
                return;
            }

            FireBDUserRecord rec;
            if (!decodeUserJson_(json, rec)) {
                if (done) done(false, FireBDUserRecord{}, "Réponse Firebase invalide.");
                return;
            }
            if (rec.phone.isEmpty()) {
                rec.phone = phone;
            }
            if (rec.passSalt.isEmpty() || rec.passHash.isEmpty()) {
                if (done) done(false, FireBDUserRecord{}, "Compte invalide (password manquant).");
                return;
            }
            const SwString computed = hashPassword_(password, rec.passSalt);
            if (computed != rec.passHash) {
                if (done) done(false, FireBDUserRecord{}, "Mot de passe incorrect.");
                return;
            }
            if (done) done(true, rec, SwString());
        });

        SwObject::connect(http, &FireBDHttpClient::errorOccurred, this, [http, done](int) {
            http->deleteLater();
            if (done) done(false, FireBDUserRecord{}, "Erreur réseau.");
        });

        http->get(url);
    }

    SwString m_baseUrl;
    SwString m_authToken;
};
