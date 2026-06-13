#include "SwJsonArray.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwJsonValue.h"
#include "SwString.h"

#include <iostream>

static int g_failed = 0;

static void expect(bool condition, const char* label) {
    if (condition) {
        std::cout << "[PASS] " << label << "\n";
    } else {
        std::cout << "[FAIL] " << label << "\n";
        ++g_failed;
    }
}

static void testJsonValueStringApi() {
    SwJsonValue value(SwString("camera"));
    expect(value.isString(), "SwJsonValue stores SwString");
    expect(value.toString() == "camera", "SwJsonValue::toString returns SwString");
    expect(value.toJsonString() == "\"camera\"", "SwJsonValue::toJsonString returns SwString JSON");
    expect(SwJsonValue::escapeString(SwString("a\nb")) == "a\\nb", "SwJsonValue::escapeString uses SwString");
}

static void testJsonObjectQtLikeApi() {
    SwJsonObject object;
    const SwString nameKey("name");
    object.insert(nameKey, SwJsonValue(SwString("ptz")));
    object["enabled"] = SwJsonValue(true);

    expect(object.contains(nameKey), "SwJsonObject::contains accepts SwString");
    expect(object.value(nameKey).toString() == "ptz", "SwJsonObject::value accepts SwString");
    expect(object.find(nameKey) != object.end(), "SwJsonObject::find accepts SwString");
    expect(object.constFind("enabled") != object.cend(), "SwJsonObject::constFind accepts literal");

    SwJsonValue taken = object.take(nameKey);
    expect(taken.toString() == "ptz", "SwJsonObject::take returns removed value");
    expect(!object.contains(nameKey), "SwJsonObject::take removes key");

    object["quote\"key"] = SwJsonValue(SwString("escaped"));
    SwString json = SwJsonDocument(object).toJson(SwJsonDocument::JsonFormat::Compact);
    expect(json.contains("\\\""), "SwJsonDocument escapes SwString keys");
}

static void testJsonArrayQtLikeApi() {
    SwJsonArray array;
    array.append(SwJsonValue(2));
    array.prepend(SwJsonValue(1));
    array.insert(2, SwJsonValue(4));
    array.replace(2, SwJsonValue(3));

    expect(array.size() == 3, "SwJsonArray size after append/prepend/insert");
    expect(array.at(0).toLongLong() == 1, "SwJsonArray::at returns value");
    expect(array.first().toLongLong() == 1, "SwJsonArray::first returns first value");
    expect(array.last().toLongLong() == 3, "SwJsonArray::last returns last value");
    expect(array.takeAt(1).toLongLong() == 2, "SwJsonArray::takeAt returns removed value");
    expect(array.size() == 2, "SwJsonArray::takeAt removes value");
    array.removeAt(1);
    expect(array.size() == 1, "SwJsonArray::removeAt removes value");
}

static void testJsonDocumentSwStringApi() {
    SwString error;
    SwJsonDocument parsed = SwJsonDocument::fromJson(
        SwString("{\"name\":\"cam\",\"letter\":\"\\u0041\",\"arr\":[1,true,\"x\"]}"),
        error);

    expect(error.isEmpty(), "SwJsonDocument::fromJson accepts SwString");
    expect(parsed.isObject(), "SwJsonDocument parsed object");
    expect(!parsed.isEmpty(), "SwJsonDocument::isEmpty reflects root");

    SwJsonObject root = parsed.object();
    expect(root.value("name").toString() == "cam", "parsed object value is SwString");
    expect(root.value("letter").toString() == "A", "unicode escape parses into SwString");
    expect(root.value("arr").toArray().at(1).toBool() == true, "nested array parsed");

    SwJsonDocument valueDoc(SwJsonValue(SwString("root")));
    expect(valueDoc.toJson(SwJsonDocument::JsonFormat::Compact) == "\"root\"", "SwJsonDocument accepts SwJsonValue root");
}

static void testJsonStringsKeepTheirType() {
    SwString error;
    SwJsonDocument parsed = SwJsonDocument::fromJson(
        SwString("{\"boolText\":\"false\",\"intText\":\"42\",\"nullText\":\"null\",\"floatText\":\"3.5\"}"),
        error);

    SwJsonObject root = parsed.object();
    expect(error.isEmpty(), "quoted scalar-looking values parse without error");
    expect(root.value("boolText").isString(), "quoted false remains string");
    expect(root.value("boolText").toString() == "false", "quoted false keeps text");
    expect(root.value("intText").isString(), "quoted integer remains string");
    expect(root.value("intText").toString() == "42", "quoted integer keeps text");
    expect(root.value("nullText").isString(), "quoted null remains string");
    expect(root.value("floatText").isString(), "quoted float remains string");
}

static void testJsonEncryptedScalarsStayValidJson() {
    SwJsonObject object;
    object["textNumber"] = SwJsonValue(SwString("42"));
    object["enabled"] = SwJsonValue(true);
    object["count"] = SwJsonValue(42);
    object["ratio"] = SwJsonValue(2.5);
    object["nothing"] = SwJsonValue();

    SwString key("json-test-key");
    SwString encryptedJson = SwJsonDocument(object).toJson(SwJsonDocument::JsonFormat::Compact, key);

    SwString rawError;
    SwJsonDocument rawParsed;
    expect(rawParsed.loadFromJson(encryptedJson, rawError), "encrypted scalar document remains valid JSON");

    SwString decryptedError;
    SwJsonDocument decrypted = SwJsonDocument::fromJson(encryptedJson, decryptedError, key);
    SwJsonObject root = decrypted.object();

    expect(decryptedError.isEmpty(), "encrypted scalar document decrypts without error");
    expect(root.value("textNumber").isString(), "encrypted scalar-looking string remains string");
    expect(root.value("textNumber").toString() == "42", "encrypted string keeps original value");
    expect(root.value("enabled").isBool() && root.value("enabled").toBool(), "encrypted bool round-trips as bool");
    expect(root.value("count").isInt() && root.value("count").toLongLong() == 42, "encrypted int round-trips as int");
    expect(root.value("ratio").isDouble() && root.value("ratio").toDouble() == 2.5, "encrypted double round-trips as double");
    expect(root.value("nothing").isNull(), "encrypted null round-trips as null");
}

static void testJsonValueDeepEquality() {
    SwJsonObject object;
    object["name"] = SwJsonValue(SwString("ptz"));
    object["enabled"] = SwJsonValue(true);

    SwJsonArray array;
    array.append(SwJsonValue(object));

    SwJsonValue objectValue(object);
    SwJsonValue copiedObjectValue(objectValue);
    SwJsonValue arrayValue(array);
    SwJsonValue copiedArrayValue(arrayValue);

    expect(objectValue == copiedObjectValue, "SwJsonValue object equality is deep");
    expect(arrayValue == copiedArrayValue, "SwJsonValue array equality is deep");
}

static void testNestedToJsonStringIsValidJson() {
    SwJsonObject child;
    child["label"] = SwJsonValue(SwString("nested"));

    SwJsonArray array;
    array.append(SwJsonValue(child));

    SwJsonObject root;
    root["items"] = SwJsonValue(array);

    SwString arrayJson = array.toJsonString();
    SwString objectJson = root.toJsonString();

    SwString arrayError;
    SwString objectError;
    SwJsonDocument arrayDoc;
    SwJsonDocument objectDoc;

    expect(!arrayJson.contains("SwJsonValue"), "array toJsonString does not emit placeholders");
    expect(!objectJson.contains("SwJsonValue"), "object toJsonString does not emit placeholders");
    expect(arrayDoc.loadFromJson(arrayJson, arrayError), "array toJsonString emits valid nested JSON");
    expect(objectDoc.loadFromJson(objectJson, objectError), "object toJsonString emits valid nested JSON");
}

int main() {
    std::cout << "=== SwJson Self-Test ===\n";
    testJsonValueStringApi();
    testJsonObjectQtLikeApi();
    testJsonArrayQtLikeApi();
    testJsonDocumentSwStringApi();
    testJsonStringsKeepTheirType();
    testJsonEncryptedScalarsStayValidJson();
    testJsonValueDeepEquality();
    testNestedToJsonStringIsValidJson();

    if (g_failed != 0) {
        std::cout << "=== Results: " << g_failed << " failed ===\n";
        return 1;
    }

    std::cout << "=== Results: all passed ===\n";
    return 0;
}
