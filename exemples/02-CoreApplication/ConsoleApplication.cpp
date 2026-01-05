#include <iostream>
#include <typeinfo>
#include <iomanip>
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwJsonArray.h"
#include "SwAny.h"

namespace
{
void printHeader(const std::string &title)
{
    std::cout << "\n===== " << title << " =====\n";
}

void printCheck(const std::string &label, bool condition)
{
    std::cout << "[CHECK] " << label << " -> " << (condition ? "OK" : "FAILED") << std::endl;
}

void printDoubleLine(const std::string &label, const SwJsonValue &value, int precision = 20)
{
    auto prev = std::cout.precision();
    std::cout << label << std::setprecision(precision) << value.toDouble(precision) << std::endl;
    std::cout.precision(prev);
}
}

int main()
{
    SwAny::registerMetaType<SwJsonDocument>();

    const std::string jsonString = R"({
        "person": {
            "name": "John Doe",
            "address": {
                "city": "Toulouse",
                "postalCode": 31000
            },
            "age": 30,
            "isRegistered": true,
            "hobbies": ["guitar", "cooking"],
            "scores": [12.5, 15.0, 18.25],
            "misc": {
                "luckyNumbers": [7, 24, 42],
                "preferred": {
                    "color": "blue",
                    "meal": "sushi"
                },
                "doubleAsInt": 42.0,
                "scientific": 1.2367867686865434545e-4
            }
        }
    })";

    std::string errorMessage;
    SwJsonDocument doc = SwJsonDocument::fromJson(jsonString, errorMessage);
    if (!errorMessage.empty())
    {
        std::cerr << "[ERROR] parsing failed: " << errorMessage << std::endl;
        return 1;
    }

    printHeader("Basic value checks");
    SwJsonValue &person = doc.find("person");
    printCheck("person isObject", person.isObject());
    SwJsonValue &name = doc.find("person/name");
    SwJsonValue &city = doc.find("person/address/city");
    SwJsonValue &postalCode = doc.find("person/address/postalCode");
    SwJsonValue &age = doc.find("person/age");
    SwJsonValue &isRegistered = doc.find("person/isRegistered");
    SwJsonValue &hobbies = doc.find("person/hobbies");
    SwJsonValue &scores = doc.find("person/scores");
    SwJsonValue &doubleAsInt = doc.find("person/misc/doubleAsInt");
    SwJsonValue &scientificVal = doc.find("person/misc/scientific");

    printCheck("name isString", name.isString());
    printCheck("city isString", city.isString());
    printCheck("postalCode reported as int", postalCode.isInt());
    printCheck("postalCode reported as double", postalCode.isDouble());
    printCheck("age reported as int", age.isInt());
    printCheck("age reported as double", age.isDouble());
    printCheck("isRegistered isBool", isRegistered.isBool());
    printCheck("hobbies isArray", hobbies.isArray());
    printCheck("scores isArray", scores.isArray());
    printCheck("doubleAsInt isInt", doubleAsInt.isInt());
    printCheck("doubleAsInt isDouble", doubleAsInt.isDouble());
    printCheck("scientific isDouble", scientificVal.isDouble());

    std::cout << "Name: " << name.toString() << std::endl;
    std::cout << "City: " << city.toString() << std::endl;
    std::cout << "Postal Code: " << postalCode.toInt() << std::endl;
    std::cout << "Age: " << age.toInt() << std::endl;
    std::cout << "Registered: " << (isRegistered.toBool() ? "true" : "false") << std::endl;
    std::cout << "Double stored as int (int): " << doubleAsInt.toInt() << std::endl;
    printDoubleLine("Double stored as int (double): ", doubleAsInt);
    printDoubleLine("Scientific value (double): ", scientificVal);

    printHeader("Testing numbers and averages");
    SwJsonArray scoresArray = *scores.toArray();
    auto scoresData = scoresArray.data();
    double sum = 0.0;
    int numericValues = 0;
    for (const auto &entry : scoresData)
    {
        if (entry.isDouble() || entry.isInt())
        {
            sum += entry.isDouble() ? entry.toDouble() : static_cast<double>(entry.toInt());
            ++numericValues;
        }
    }
    double average = numericValues == 0 ? 0.0 : sum / numericValues;
    std::cout << "Average score: " << average << std::endl;
    scoresArray.append(20.0);
    scores = scoresArray;

    printHeader("Manipulating arrays and strings");
    SwJsonArray hobbiesArray = *hobbies.toArray();
    hobbiesArray.append("coding");
    hobbiesArray.append("bouldering");
    hobbies = hobbiesArray;
    std::cout << "Hobbies now contains " << hobbiesArray.size() << " entries." << std::endl;

    printHeader("Testing boolean toggling and null");
    isRegistered = SwJsonValue(!isRegistered.toBool());
    printCheck("isRegistered toggled correctly", isRegistered.isBool());
    SwJsonValue nullValue;
    printCheck("default SwJsonValue isNull", nullValue.isNull());

    printHeader("Working with nested objects");
    SwJsonValue &preferred = doc.find("person/misc/preferred");
    printCheck("preferred isObject", preferred.isObject());
    SwJsonObject preferredObj = *preferred.toObject();
    std::cout << "Preferred color: " << preferredObj["color"].toString() << std::endl;
    preferredObj.insert("drink", SwJsonValue("coffee"));
    preferred = preferredObj;

    printHeader("Using SwAny conversions");
    SwAny docAnyPtr = SwAny::fromVoidPtr(reinterpret_cast<void *>(&doc), typeid(SwJsonDocument).name());
    printCheck("SwAny from void ptr stores document", !docAnyPtr.get<SwJsonDocument>().toJson().isEmpty());
    SwAny docAnyCopy = SwAny::from(doc);
    printCheck("SwAny from copy stores document", !docAnyCopy.get<SwJsonDocument>().toJson().isEmpty());

    printHeader("Creating new sections dynamically");
    SwJsonValue &notes = doc.find("person/metadata/notes", true);
    notes = SwJsonValue("First note");
    SwJsonValue &tags = doc.find("person/metadata/tags", true);
    SwJsonArray tagArray;
    tagArray.append("json");
    tagArray.append("tests");
    tags = tagArray;

    printHeader("Final JSON output");
    std::cout << doc.toJson(SwJsonDocument::JsonFormat::Pretty) << std::endl;

    return 0;
}
