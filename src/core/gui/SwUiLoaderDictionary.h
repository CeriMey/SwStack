#pragma once

#include "SwString.h"

namespace swui::compat {

inline SwString widgetClassToSw(const SwString& className) {
    if (className == "QWidget") return "SwWidget";
    if (className == "QFrame") return "SwFrame";
    if (className == "QLabel") return "SwLabel";
    if (className == "QPushButton") return "SwPushButton";
    if (className == "QToolButton") return "SwToolButton";
    if (className == "QLineEdit") return "SwLineEdit";
    if (className == "QCheckBox") return "SwCheckBox";
    if (className == "QRadioButton") return "SwRadioButton";
    if (className == "QComboBox") return "SwComboBox";
    if (className == "QProgressBar") return "SwProgressBar";
    if (className == "QPlainTextEdit") return "SwPlainTextEdit";
    if (className == "QTextEdit") return "SwTextEdit";
    if (className == "QTabWidget") return "SwTabWidget";
    if (className == "QSplitter") return "SwSplitter";
    if (className == "QStackedWidget") return "SwStackedWidget";
    if (className == "QScrollArea") return "SwScrollArea";
    if (className == "QSpacerItem") return "SwSpacer";
    if (className == "QGroupBox") return "SwGroupBox";
    if (className == "QToolBox") return "SwToolBox";
    if (className == "QSpinBox") return "SwSpinBox";
    if (className == "QDoubleSpinBox") return "SwDoubleSpinBox";
    if (className == "QSlider") return "SwSlider";
    if (className == "QTableWidget") return "SwTableWidget";
    if (className == "QTreeWidget") return "SwTreeWidget";
    if (className == "QTableView") return "SwTableView";
    if (className == "QTreeView") return "SwTreeView";
    if (className == "QMainWindow") return "SwMainWindow";

    if (!className.isEmpty() && className[0] == 'Q') {
        return SwString("Sw") + className.mid(1);
    }
    return className;
}

inline SwString propertyNameToSw(SwString name) {
    if (name == "objectName") return "ObjectName";
    if (name == "toolTip") return "ToolTips";
    if (name == "styleSheet") return "StyleSheet";
    if (name == "enabled") return "Enable";
    if (name == "visible") return "Visible";
    if (name == "text") return "Text";
    if (name == "title") return "Text";
    if (name == "placeholderText") return "Placeholder";
    if (name == "readOnly") return "ReadOnly";
    if (name == "geometry") return "geometry";

    if (!name.isEmpty() && name[0] >= 'a' && name[0] <= 'z') {
        name[0] = static_cast<char>(name[0] - ('a' - 'A'));
    }
    return name;
}

inline SwString normalizeMemberName(SwString member) {
    member = member.trimmed();
    while (!member.isEmpty() && member[0] >= '0' && member[0] <= '9') {
        member = member.mid(1).trimmed();
    }

    const int paren = member.indexOf('(');
    if (paren >= 0) {
        member = member.left(paren).trimmed();
    }

    const int scope = member.indexOf("::");
    if (scope >= 0) {
        member = member.mid(scope + 2).trimmed();
    }
    return member;
}

inline SwString setterSlotToPropertyName(const SwString& slotName) {
    SwString propertyName = slotName;
    if (slotName.size() > 3 && slotName.left(3).toLower() == "set") {
        propertyName = slotName.mid(3);
    }

    if (propertyName == "Enabled" || propertyName == "Disabled") {
        return "Enable";
    }
    return propertyNameToSw(propertyName);
}

} // namespace swui::compat
