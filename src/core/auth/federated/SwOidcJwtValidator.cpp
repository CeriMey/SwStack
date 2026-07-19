#include "auth/federated/SwOidcJwtValidator.h"

#include "SwJsonArray.h"
#include "SwJsonDocument.h"
#include "auth/SwHttpAuthTypes.h"

#include <ctime>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

bool SwOidcJwtValidator::validate(const SwString& idToken,
                                  const SwJsonObject& jwks,
                                  const SwFederatedProviderDefinition& provider,
                                  const SwString& clientId,
                                  const SwString& expectedNonce,
                                  SwFederatedIdentity& outIdentity,
                                  SwString* errorOut) const {
    outIdentity = SwFederatedIdentity();
    if (errorOut) {
        errorOut->clear();
    }

    SwString headerB64;
    SwString payloadB64;
    SwString signatureB64;
    if (!splitJwt_(idToken, headerB64, payloadB64, signatureB64)) {
        return fail_(errorOut, "Invalid OIDC id_token");
    }

    SwJsonObject header;
    SwJsonObject payload;
    if (!decodeJson_(headerB64, header) || !decodeJson_(payloadB64, payload)) {
        return fail_(errorOut, "Invalid OIDC id_token payload");
    }
    if (header.value("alg").toString() != "RS256") {
        return fail_(errorOut, "Unsupported OIDC signing algorithm");
    }
    if (!verifyRs256_(headerB64 + "." + payloadB64,
                      signatureB64,
                      jwks,
                      header,
                      errorOut)) {
        return false;
    }
    if (!issuerMatches_(payload.value("iss").toString(), provider)) {
        return fail_(errorOut, "OIDC issuer mismatch");
    }
    if (!audienceMatches_(payload.value("aud"), clientId)) {
        return fail_(errorOut, "OIDC audience mismatch");
    }
    const SwJsonValue audience = payload.value("aud");
    const SwString authorizedParty = payload.value("azp").toString().trimmed();
    if ((!authorizedParty.isEmpty() && authorizedParty != clientId.trimmed()) ||
        (audience.isArray() && audience.toArray().size() > 1 && authorizedParty.isEmpty())) {
        return fail_(errorOut, "OIDC authorized party mismatch");
    }

    const long long now = static_cast<long long>(std::time(nullptr));
    if (payload.value("exp").toInteger(0) <= now) {
        return fail_(errorOut, "OIDC id_token expired");
    }
    const long long notBefore = payload.value("nbf").toInteger(0);
    if (notBefore > 0 && notBefore > now + 60) {
        return fail_(errorOut, "OIDC id_token is not active");
    }
    const long long issuedAt = payload.value("iat").toInteger(0);
    if (issuedAt > 0 && issuedAt > now + 60) {
        return fail_(errorOut, "OIDC id_token issued in the future");
    }
    if (provider.requiresNonce &&
        payload.value("nonce").toString() != expectedNonce.trimmed()) {
        return fail_(errorOut, "OIDC nonce mismatch");
    }

    outIdentity.provider = provider.key;
    outIdentity.providerSubject = payload.value("sub").toString().trimmed();
    outIdentity.email = swHttpAuthDetail::normalizeEmail(payload.value("email").toString());
    outIdentity.emailVerified = truthy_(payload.value("email_verified"));
    outIdentity.displayName = payload.value("name").toString().trimmed();
    outIdentity.pictureUrl = payload.value("picture").toString().trimmed();
    outIdentity.rawClaims = payload;

    if (outIdentity.providerSubject.isEmpty()) {
        return fail_(errorOut, "OIDC subject missing");
    }
    if (outIdentity.email.isEmpty()) {
        return fail_(errorOut, "OIDC email missing");
    }
    if (!outIdentity.emailVerified) {
        return fail_(errorOut, "OIDC email was not verified by provider");
    }
    return true;
}

bool SwOidcJwtValidator::splitJwt_(const SwString& token,
                                   SwString& headerB64,
                                   SwString& payloadB64,
                                   SwString& signatureB64) {
    const SwString value = token.trimmed();
    const int firstDot = value.indexOf('.');
    const int secondDot = firstDot < 0
                              ? -1
                              : value.indexOf('.', static_cast<std::size_t>(firstDot + 1));
    if (firstDot <= 0 || secondDot <= firstDot + 1 ||
        secondDot >= static_cast<int>(value.size()) - 1) {
        return false;
    }
    headerB64 = value.left(firstDot);
    payloadB64 = value.mid(firstDot + 1, secondDot - firstDot - 1);
    signatureB64 = value.mid(secondDot + 1);
    return true;
}

bool SwOidcJwtValidator::decodeJson_(const SwString& encoded, SwJsonObject& outObject) {
    bool decodedOk = false;
    const SwByteArray bytes = base64UrlDecode_(encoded, &decodedOk);
    if (!decodedOk) {
        return false;
    }
    SwString error;
    const SwJsonDocument document = SwJsonDocument::fromJson(bytes.toStdString(), error);
    if (!error.isEmpty() || !document.isObject()) {
        return false;
    }
    outObject = document.object();
    return true;
}

bool SwOidcJwtValidator::verifyRs256_(const SwString& signingInput,
                                      const SwString& signatureB64,
                                      const SwJsonObject& jwks,
                                      const SwJsonObject& header,
                                      SwString* errorOut) {
    const SwString keyId = header.value("kid").toString().trimmed();
    const SwJsonValue keysValue = jwks.value("keys");
    if (keyId.isEmpty() || !keysValue.isArray()) {
        return fail_(errorOut, "OIDC JWKS is missing key material");
    }

    SwJsonObject selectedKey;
    const SwJsonArray keys = keysValue.toArray();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (!keys[i].isObject()) {
            continue;
        }
        const SwJsonObject key = keys[i].toObject();
        if (key.value("kid").toString() == keyId &&
            key.value("kty").toString() == "RSA") {
            selectedKey = key;
            break;
        }
    }
    if (selectedKey.isEmpty()) {
        return fail_(errorOut, "OIDC signing key not found");
    }

    bool modulusOk = false;
    bool exponentOk = false;
    bool signatureOk = false;
    const SwByteArray modulus = base64UrlDecode_(selectedKey.value("n").toString(), &modulusOk);
    const SwByteArray exponent = base64UrlDecode_(selectedKey.value("e").toString(), &exponentOk);
    const SwByteArray signature = base64UrlDecode_(signatureB64, &signatureOk);
    if (!modulusOk || !exponentOk || !signatureOk) {
        return fail_(errorOut, "OIDC signing key decode failed");
    }

    BIGNUM* n = BN_bin2bn(reinterpret_cast<const unsigned char*>(modulus.constData()),
                          static_cast<int>(modulus.size()),
                          nullptr);
    BIGNUM* e = BN_bin2bn(reinterpret_cast<const unsigned char*>(exponent.constData()),
                          static_cast<int>(exponent.size()),
                          nullptr);
    RSA* rsa = RSA_new();
    if (!n || !e || !rsa || RSA_set0_key(rsa, n, e, nullptr) != 1) {
        if (n) BN_free(n);
        if (e) BN_free(e);
        if (rsa) RSA_free(rsa);
        return fail_(errorOut, "OIDC public key build failed");
    }

    EVP_PKEY* key = EVP_PKEY_new();
    if (!key || EVP_PKEY_assign_RSA(key, rsa) != 1) {
        RSA_free(rsa);
        if (key) EVP_PKEY_free(key);
        return fail_(errorOut, "OIDC public key build failed");
    }

    EVP_MD_CTX* context = EVP_MD_CTX_new();
    const std::string bytes = signingInput.toStdString();
    const int initOk = context
                           ? EVP_DigestVerifyInit(context, nullptr, EVP_sha256(), nullptr, key)
                           : 0;
    const int updateOk = initOk == 1
                             ? EVP_DigestVerifyUpdate(context, bytes.data(), bytes.size())
                             : 0;
    const int finalOk = updateOk == 1
                            ? EVP_DigestVerifyFinal(
                                  context,
                                  reinterpret_cast<const unsigned char*>(signature.constData()),
                                  signature.size())
                            : 0;
    if (context) EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    if (finalOk != 1) {
        return fail_(errorOut, "OIDC id_token signature invalid");
    }
    return true;
}

bool SwOidcJwtValidator::issuerMatches_(const SwString& issuer,
                                        const SwFederatedProviderDefinition& provider) {
    SwString normalized = issuer.trimmed();
    while (normalized.size() > 8 && normalized.endsWith("/")) {
        normalized.chop(1);
    }
    std::vector<SwString> accepted = provider.acceptedIssuers;
    accepted.push_back(provider.issuer);
    for (std::size_t i = 0; i < accepted.size(); ++i) {
        SwString expected = accepted[i].trimmed();
        while (expected.size() > 8 && expected.endsWith("/")) {
            expected.chop(1);
        }
        if (!expected.isEmpty() && normalized == expected) {
            return true;
        }
    }
    return false;
}

bool SwOidcJwtValidator::audienceMatches_(const SwJsonValue& audience,
                                          const SwString& clientId) {
    const SwString expected = clientId.trimmed();
    if (expected.isEmpty()) {
        return false;
    }
    if (audience.isString()) {
        return audience.toString() == expected;
    }
    if (!audience.isArray()) {
        return false;
    }
    const SwJsonArray values = audience.toArray();
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i].toString() == expected) {
            return true;
        }
    }
    return false;
}

bool SwOidcJwtValidator::truthy_(const SwJsonValue& value) {
    if (value.isBool()) {
        return value.toBool(false);
    }
    const SwString text = value.toString().trimmed().toLower();
    return text == "true" || text == "1";
}

SwByteArray SwOidcJwtValidator::base64UrlDecode_(SwString encoded, bool* okOut) {
    if (okOut) {
        *okOut = false;
    }
    encoded = encoded.trimmed();
    if (encoded.isEmpty()) {
        return SwByteArray();
    }
    encoded.replace("-", "+");
    encoded.replace("_", "/");
    while ((encoded.size() % 4) != 0) {
        encoded += "=";
    }
    const SwByteArray result = SwByteArray::fromBase64(SwByteArray(encoded.toStdString()));
    if (okOut) {
        *okOut = !result.isEmpty();
    }
    return result;
}

bool SwOidcJwtValidator::fail_(SwString* errorOut, const SwString& message) {
    if (errorOut) {
        *errorOut = message;
    }
    return false;
}
