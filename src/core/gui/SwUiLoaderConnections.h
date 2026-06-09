#pragma once

#include "SwCheckBox.h"
#include "SwComboBox.h"
#include "SwDialog.h"
#include "SwDoubleSpinBox.h"
#include "SwGroupBox.h"
#include "SwLabel.h"
#include "SwLineEdit.h"
#include "SwPlainTextEdit.h"
#include "SwProgressBar.h"
#include "SwPushButton.h"
#include "SwRadioButton.h"
#include "SwSlider.h"
#include "SwSpinBox.h"
#include "SwStackedWidget.h"
#include "SwTabWidget.h"
#include "SwTextEdit.h"
#include "SwToolButton.h"
#include "SwToolBox.h"
#include "SwUiLoaderDictionary.h"
#include "SwXmlDocument.h"

namespace swui::detail {

struct UiConnection {
    SwString senderName;
    SwString signalName;
    SwString receiverName;
    SwString slotName;
};

inline SwString childText(const SwXmlNode& node, const char* childName) {
    const SwXmlNode* child = node.firstChild(childName);
    return child ? child->text.trimmed() : SwString();
}

inline UiConnection parseConnection(const SwXmlNode& connectionNode) {
    UiConnection connection;
    connection.senderName = childText(connectionNode, "sender");
    if (connection.senderName.isEmpty()) {
        connection.senderName = connectionNode.attr("sender");
    }

    connection.signalName = compat::normalizeMemberName(childText(connectionNode, "signal"));
    if (connection.signalName.isEmpty()) {
        connection.signalName = compat::normalizeMemberName(connectionNode.attr("signal"));
    }

    connection.receiverName = childText(connectionNode, "receiver");
    if (connection.receiverName.isEmpty()) {
        connection.receiverName = connectionNode.attr("receiver");
    }

    connection.slotName = compat::normalizeMemberName(childText(connectionNode, "slot"));
    if (connection.slotName.isEmpty()) {
        connection.slotName = compat::normalizeMemberName(connectionNode.attr("slot"));
    }
    return connection;
}

inline SwWidget* findNamedWidget(SwWidget* root, const SwString& name) {
    if (!root || name.isEmpty()) {
        return nullptr;
    }
    if (root->getObjectName() == name) {
        return root;
    }
    return root->findChild<SwWidget>(name);
}

inline bool invokeSlot(SwWidget* receiver, const SwString& slotName) {
    if (!receiver) {
        return false;
    }

    const SwString slot = slotName.toLower();
    if (slot == "show") {
        receiver->show();
        return true;
    }
    if (slot == "hide" || slot == "close") {
        receiver->hide();
        return true;
    }
    if (slot == "accept" || slot == "accepted") {
        if (auto* dialog = dynamic_cast<SwDialog*>(receiver)) {
            dialog->accept();
            return true;
        }
    }
    if (slot == "reject" || slot == "rejected") {
        if (auto* dialog = dynamic_cast<SwDialog*>(receiver)) {
            dialog->reject();
            return true;
        }
    }
    if (slot == "clear") {
        if (auto* edit = dynamic_cast<SwLineEdit*>(receiver)) {
            edit->setText(SwString());
            return true;
        }
        if (auto* edit = dynamic_cast<SwPlainTextEdit*>(receiver)) {
            edit->clear();
            return true;
        }
        if (auto* edit = dynamic_cast<SwTextEdit*>(receiver)) {
            edit->clear();
            return true;
        }
        if (auto* combo = dynamic_cast<SwComboBox*>(receiver)) {
            combo->clear();
            return true;
        }
    }
    return false;
}

inline bool invokeSlot(SwWidget* receiver, const SwString& slotName, bool value) {
    if (!receiver) {
        return false;
    }

    const SwString slot = slotName.toLower();
    if (slot == "setvisible" || slot == "visible" || slot == "show") {
        receiver->setVisible(value);
        return true;
    }
    if (slot == "sethidden" || slot == "hide") {
        receiver->setVisible(!value);
        return true;
    }
    if (slot == "setenabled" || slot == "setenable" || slot == "enabled") {
        receiver->setProperty("Enable", SwAny(value));
        return true;
    }
    if (slot == "setdisabled" || slot == "disabled") {
        receiver->setProperty("Enable", SwAny(!value));
        return true;
    }
    if (slot == "setchecked" || slot == "checked" || slot == "toggle") {
        if (auto* cb = dynamic_cast<SwCheckBox*>(receiver)) {
            cb->setChecked(value);
            return true;
        }
        if (auto* rb = dynamic_cast<SwRadioButton*>(receiver)) {
            rb->setChecked(value);
            return true;
        }
        if (auto* pb = dynamic_cast<SwPushButton*>(receiver)) {
            pb->setCheckable(true);
            pb->setChecked(value);
            return true;
        }
        if (auto* tb = dynamic_cast<SwToolButton*>(receiver)) {
            tb->setCheckable(true);
            tb->setChecked(value);
            return true;
        }
    }

    const SwString propertyName = compat::setterSlotToPropertyName(slotName);
    if (!propertyName.isEmpty()) {
        receiver->setProperty(propertyName, SwAny(value));
        return true;
    }
    return false;
}

inline bool invokeSlot(SwWidget* receiver, const SwString& slotName, int value) {
    if (!receiver) {
        return false;
    }

    const SwString slot = slotName.toLower();
    if (slot == "setcurrentindex") {
        if (auto* combo = dynamic_cast<SwComboBox*>(receiver)) {
            combo->setCurrentIndex(value);
            return true;
        }
        if (auto* tabs = dynamic_cast<SwTabWidget*>(receiver)) {
            tabs->setCurrentIndex(value);
            return true;
        }
        if (auto* stack = dynamic_cast<SwStackedWidget*>(receiver)) {
            stack->setCurrentIndex(value);
            return true;
        }
        if (auto* toolbox = dynamic_cast<SwToolBox*>(receiver)) {
            toolbox->setCurrentIndex(value);
            return true;
        }
    }
    if (slot == "setvalue") {
        if (auto* spin = dynamic_cast<SwSpinBox*>(receiver)) {
            spin->setValue(value);
            return true;
        }
        if (auto* dspin = dynamic_cast<SwDoubleSpinBox*>(receiver)) {
            dspin->setValue(static_cast<double>(value));
            return true;
        }
        if (auto* slider = dynamic_cast<SwSlider*>(receiver)) {
            slider->setValue(value);
            return true;
        }
        if (auto* progress = dynamic_cast<SwProgressBar*>(receiver)) {
            progress->setValue(value);
            return true;
        }
    }

    const SwString propertyName = compat::setterSlotToPropertyName(slotName);
    if (!propertyName.isEmpty()) {
        receiver->setProperty(propertyName, SwAny(value));
        return true;
    }
    return false;
}

inline bool invokeSlot(SwWidget* receiver, const SwString& slotName, double value) {
    if (!receiver) {
        return false;
    }

    const SwString slot = slotName.toLower();
    if (slot == "setvalue") {
        if (auto* dspin = dynamic_cast<SwDoubleSpinBox*>(receiver)) {
            dspin->setValue(value);
            return true;
        }
        if (auto* spin = dynamic_cast<SwSpinBox*>(receiver)) {
            spin->setValue(static_cast<int>(value));
            return true;
        }
        if (auto* slider = dynamic_cast<SwSlider*>(receiver)) {
            slider->setValue(static_cast<int>(value));
            return true;
        }
        if (auto* progress = dynamic_cast<SwProgressBar*>(receiver)) {
            progress->setValue(static_cast<int>(value));
            return true;
        }
    }

    const SwString propertyName = compat::setterSlotToPropertyName(slotName);
    if (!propertyName.isEmpty()) {
        receiver->setProperty(propertyName, SwAny(value));
        return true;
    }
    return false;
}

inline bool invokeSlot(SwWidget* receiver, const SwString& slotName, const SwString& value) {
    if (!receiver) {
        return false;
    }

    const SwString slot = slotName.toLower();
    if (slot == "settext" || slot == "text") {
        if (auto* edit = dynamic_cast<SwLineEdit*>(receiver)) {
            edit->setText(value);
            return true;
        }
        if (auto* label = dynamic_cast<SwLabel*>(receiver)) {
            label->setText(value);
            return true;
        }
        if (auto* button = dynamic_cast<SwPushButton*>(receiver)) {
            button->setText(value);
            return true;
        }
        if (auto* button = dynamic_cast<SwToolButton*>(receiver)) {
            button->setText(value);
            return true;
        }
        if (auto* checkbox = dynamic_cast<SwCheckBox*>(receiver)) {
            checkbox->setText(value);
            return true;
        }
        if (auto* radio = dynamic_cast<SwRadioButton*>(receiver)) {
            radio->setText(value);
            return true;
        }
        if (auto* edit = dynamic_cast<SwTextEdit*>(receiver)) {
            edit->setText(value);
            return true;
        }
        if (auto* edit = dynamic_cast<SwPlainTextEdit*>(receiver)) {
            edit->setPlainText(value);
            return true;
        }
    }
    if (slot == "setplaintext") {
        if (auto* edit = dynamic_cast<SwPlainTextEdit*>(receiver)) {
            edit->setPlainText(value);
            return true;
        }
        if (auto* edit = dynamic_cast<SwTextEdit*>(receiver)) {
            edit->setPlainText(value);
            return true;
        }
    }
    if (slot == "sethtml") {
        if (auto* edit = dynamic_cast<SwTextEdit*>(receiver)) {
            edit->setHtml(value);
            return true;
        }
    }
    if (slot == "setcurrenttext") {
        if (auto* combo = dynamic_cast<SwComboBox*>(receiver)) {
            for (int i = 0; i < combo->count(); ++i) {
                if (combo->itemText(i) == value) {
                    combo->setCurrentIndex(i);
                    return true;
                }
            }
            return false;
        }
    }

    const SwString propertyName = compat::setterSlotToPropertyName(slotName);
    if (!propertyName.isEmpty()) {
        receiver->setProperty(propertyName, SwAny(value));
        return true;
    }
    return false;
}

enum class UiSignalPayload {
    Unsupported,
    Void,
    Bool,
    Int,
    Double,
    String
};

struct UiSignalBinding {
    SwString name;
    UiSignalPayload payload{UiSignalPayload::Unsupported};
};

inline UiSignalBinding signalBindingFor(SwWidget* sender, const SwString& signalName) {
    if (!sender) {
        return {};
    }

    const SwString signal = signalName.toLower();
    if (signal == "clicked") {
        if (dynamic_cast<SwPushButton*>(sender)) return {"clicked", UiSignalPayload::Void};
        if (dynamic_cast<SwToolButton*>(sender) ||
            dynamic_cast<SwCheckBox*>(sender) ||
            dynamic_cast<SwRadioButton*>(sender)) {
            return {"clicked", UiSignalPayload::Bool};
        }
    }

    if (signal == "toggled") {
        if (dynamic_cast<SwPushButton*>(sender) ||
            dynamic_cast<SwToolButton*>(sender) ||
            dynamic_cast<SwCheckBox*>(sender) ||
            dynamic_cast<SwRadioButton*>(sender) ||
            dynamic_cast<SwGroupBox*>(sender)) {
            return {"toggled", UiSignalPayload::Bool};
        }
    }

    if (signal == "statechanged" && dynamic_cast<SwCheckBox*>(sender)) {
        return {"stateChanged", UiSignalPayload::Int};
    }

    if ((signal == "currentindexchanged" || signal == "activated" || signal == "highlighted") &&
        dynamic_cast<SwComboBox*>(sender)) {
        if (signal == "currentindexchanged") return {"currentIndexChanged", UiSignalPayload::Int};
        if (signal == "activated") return {"activated", UiSignalPayload::Int};
        return {"highlighted", UiSignalPayload::Int};
    }

    if (signal == "currenttextchanged" && dynamic_cast<SwComboBox*>(sender)) {
        return {"currentTextChanged", UiSignalPayload::String};
    }

    if (signal == "currentchanged") {
        if (dynamic_cast<SwTabWidget*>(sender) ||
            dynamic_cast<SwStackedWidget*>(sender) ||
            dynamic_cast<SwToolBox*>(sender)) {
            return {"currentChanged", UiSignalPayload::Int};
        }
    }

    if (signal == "valuechanged") {
        if (dynamic_cast<SwDoubleSpinBox*>(sender)) return {"valueChanged", UiSignalPayload::Double};
        if (dynamic_cast<SwSpinBox*>(sender) ||
            dynamic_cast<SwSlider*>(sender) ||
            dynamic_cast<SwProgressBar*>(sender)) {
            return {"valueChanged", UiSignalPayload::Int};
        }
    }

    if (signal == "textchanged" || signal == "textedited") {
        if (dynamic_cast<SwLineEdit*>(sender)) return {"TextChanged", UiSignalPayload::String};
        if (dynamic_cast<SwPlainTextEdit*>(sender) || dynamic_cast<SwTextEdit*>(sender)) {
            return {"textChanged", UiSignalPayload::Void};
        }
    }

    if (signal == "visibilitychanged") {
        return {"visibilityChanged", UiSignalPayload::Void};
    }

    if (signal == "focuschanged") {
        return {"FocusChanged", UiSignalPayload::Bool};
    }

    return {};
}

inline bool connectSignalToSlot(SwWidget* sender, SwWidget* receiver, const UiConnection& connection) {
    if (!sender || !receiver || connection.signalName.isEmpty() || connection.slotName.isEmpty()) {
        return false;
    }

    const UiSignalBinding binding = signalBindingFor(sender, connection.signalName);
    if (binding.payload == UiSignalPayload::Unsupported || binding.name.isEmpty()) {
        return false;
    }

    const SwString slot = connection.slotName;
    switch (binding.payload) {
    case UiSignalPayload::Void:
        SwObject::connect(sender, binding.name, receiver, [receiver, slot]() { invokeSlot(receiver, slot); });
        return true;
    case UiSignalPayload::Bool:
        SwObject::connect(sender, binding.name, receiver, [receiver, slot](bool value) { invokeSlot(receiver, slot, value); });
        return true;
    case UiSignalPayload::Int:
        SwObject::connect(sender, binding.name, receiver, [receiver, slot](int value) { invokeSlot(receiver, slot, value); });
        return true;
    case UiSignalPayload::Double:
        SwObject::connect(sender, binding.name, receiver, [receiver, slot](double value) { invokeSlot(receiver, slot, value); });
        return true;
    case UiSignalPayload::String:
        SwObject::connect(sender, binding.name, receiver, [receiver, slot](const SwString& value) { invokeSlot(receiver, slot, value); });
        return true;
    case UiSignalPayload::Unsupported:
    default:
        return false;
    }
}

inline void connectOne(SwWidget* root, const SwXmlNode& connectionNode) {
    const UiConnection connection = parseConnection(connectionNode);
    if (connection.senderName.isEmpty() || connection.receiverName.isEmpty() ||
        connection.signalName.isEmpty() || connection.slotName.isEmpty()) {
        return;
    }

    SwWidget* sender = findNamedWidget(root, connection.senderName);
    SwWidget* receiver = findNamedWidget(root, connection.receiverName);
    if (!sender || !receiver) {
        return;
    }

    (void)connectSignalToSlot(sender, receiver, connection);
}

inline void connectDeclaredUiConnections(SwWidget* root, const SwXmlNode& uiRoot) {
    if (!root) {
        return;
    }

    const SwXmlNode* connectionsNode = uiRoot.firstChild("connections");
    if (connectionsNode) {
        for (const auto* connectionNode : connectionsNode->childrenNamed("connection")) {
            if (connectionNode) {
                connectOne(root, *connectionNode);
            }
        }
        for (const auto* connectionNode : connectionsNode->childrenNamed("connect")) {
            if (connectionNode) {
                connectOne(root, *connectionNode);
            }
        }
    }

    for (const auto* connectionNode : uiRoot.childrenNamed("connect")) {
        if (connectionNode) {
            connectOne(root, *connectionNode);
        }
    }
}

} // namespace swui::detail
