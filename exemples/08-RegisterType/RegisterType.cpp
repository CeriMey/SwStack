#include "SwAny.h"
#include "SwString.h"
#include <iostream>

// Définition d'une classe personnalisée
class CustomType {
public:
    CustomType() : m_value(0), m_name("Default") {} // Constructeur par défaut
    CustomType(int value, const std::string& name)
        : m_value(value), m_name(name) {}

    int value() const { return m_value; }
    std::string name() const { return m_name; }

private:
    int m_value;
    std::string m_name;
};


int main() {

    SwAny::registerMetaType<CustomType>();


    SwAny::registerConversion<CustomType, SwString>([](const CustomType& custom) {
        return SwString("CustomType[value=" + std::to_string(custom.value()) +
                        ", name=" + custom.name() + "]");
    });

    CustomType myCustomType(42, "ExampleType");

    SwAny anyCustom = SwAny::from(myCustomType);

    if (anyCustom.canConvert<SwString>()) {
        SwString asString = anyCustom.toString();
        std::cout << "Conversion réussie: " << asString.toStdString() << std::endl;
    } else {
        std::cerr << "Erreur : impossible de convertir CustomType en SwString." << std::endl;
    }

    return 0;
}
