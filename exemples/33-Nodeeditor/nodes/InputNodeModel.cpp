#include "InputNodeModel.h"

#include "SwLineEdit.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>

namespace swnodeeditor {

static bool tryParseDoubleLoose_(const SwString& text, double* out)
{
    if (!out) {
        return false;
    }

    std::string s = text.toStdString();
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };

    size_t start = 0;
    while (start < s.size() && isSpace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && isSpace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    s = s.substr(start, end - start);
    if (s.empty()) {
        return false;
    }

    for (char& c : s) {
        if (c == ',') {
            c = '.';
        }
    }

    errno = 0;
    char* endPtr = nullptr;
    const double v = std::strtod(s.c_str(), &endPtr);
    if (endPtr == s.c_str()) {
        return false;
    }
    while (endPtr && *endPtr && isSpace(static_cast<unsigned char>(*endPtr))) {
        ++endPtr;
    }
    if (endPtr && *endPtr) {
        return false;
    }
    if (errno == ERANGE) {
        return false;
    }
    *out = v;
    return true;
}

InputNodeModel::InputNodeModel(const SwString& context)
    : SwizioNodes::NodeDelegateModel(context)
{
}

InputNodeModel::~InputNodeModel() = default;

SwString InputNodeModel::Name() { return "Input"; }

SwString InputNodeModel::caption() const { return "Input"; }

SwString InputNodeModel::name() const { return Name(); }

bool InputNodeModel::portCaptionVisible(SwizioNodes::PortType, SwizioNodes::PortIndex) const { return true; }

SwString InputNodeModel::portCaption(SwizioNodes::PortType portType, SwizioNodes::PortIndex portIndex) const
{
    if (portType == SwizioNodes::PortType::Out && portIndex == 0) {
        return "value";
    }
    return SwString();
}

unsigned int InputNodeModel::nPorts(SwizioNodes::PortType portType) const
{
    return (portType == SwizioNodes::PortType::Out) ? 1u : 0u;
}

SwizioNodes::NodeDataType InputNodeModel::dataType(SwizioNodes::PortType, SwizioNodes::PortIndex) const
{
    return AnyListData::Type();
}

std::shared_ptr<SwizioNodes::NodeData> InputNodeModel::outData(SwizioNodes::PortIndex const port)
{
    if (port != 0) {
        return nullptr;
    }
    return m_current;
}

SwWidget* InputNodeModel::embeddedWidget()
{
    if (!m_widget) {
        std::unique_ptr<SwLineEdit> widget(new SwLineEdit("42", nullptr));
        widget->resize(140, 34);
        widget->setText("42");
        widget->setStyleSheet(R"(
                SwLineEdit {
                    background-color: rgb(30, 41, 59);
                    border-color: rgb(71, 85, 105);
                    border-width: 1px;
                    border-radius: 10px;
                    color: rgb(226, 232, 240);
                    padding: 6px;
                }
            )");

        // Mark as externally owned (model) so SwGraphicsView does not delete it when proxy items disappear.
        widget->setProperty("sw.graphics.proxy.deleteOnRemove", SwAny(false));

        // Keep the node output in sync with the widget.
        SwObject::connect(widget.get(), &SwLineEdit::TextChanged, this, [this](const SwString& t) {
            double v = 0.0;
            const bool ok = tryParseDoubleLoose_(t, &v);
            setValue(v, ok);
        });

        m_widget = std::move(widget);

        // Initialize the output from the initial text (setText() may not emit TextChanged).
        double v = 0.0;
        const bool ok = tryParseDoubleLoose_(m_widget->getText(), &v);
        setValue(v, ok);
    }

    return m_widget.get();
}

void InputNodeModel::setValue(double v, bool valid)
{
    bool changed = true;
    if (m_current) {
        const auto prev = m_current->readNumber();
        if (prev.valid == valid) {
            const double diff = std::abs(prev.value - v);
            const double scale = std::max(1.0, std::max(std::abs(prev.value), std::abs(v)));
            changed = diff > 1e-12 * scale;
        }
    }
    m_current = std::make_shared<AnyListData>(AnyListData::makeNumber(v, valid));
    if (changed) {
        dataUpdated(0);
    }
}

} // namespace swnodeeditor
