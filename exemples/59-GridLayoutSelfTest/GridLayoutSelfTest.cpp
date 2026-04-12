#include "SwGuiApplication.h"
#include "SwLayout.h"
#include "SwPushButton.h"
#include "SwWidget.h"

#include <iostream>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[GridLayoutSelfTest] FAIL: " << message << "\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    SW_UNUSED(argc);
    SW_UNUSED(argv);
    SwGuiApplication app;

    SwWidget root(nullptr);
    root.resize(640, 480);

    auto* grid = new SwGridLayout(&root);
    root.setLayout(grid);

    SwPushButton a("A", &root);
    SwPushButton b("B", &root);
    SwPushButton c("C", &root);
    SwPushButton d("D", &root);

    grid->addWidget(&a, 0, 0);
    grid->addWidget(&b, 0, 1);
    grid->addWidget(&c, 1, 0);

    bool ok = true;
    ok &= expect(grid->rowCount() == 2, "rowCount should reflect occupied rows");
    ok &= expect(grid->columnCount() == 2, "columnCount should reflect occupied columns");
    ok &= expect(grid->itemAtPosition(0, 0) == &a, "cell (0,0) should contain A");
    ok &= expect(grid->itemAtPosition(0, 1) == &b, "cell (0,1) should contain B");
    ok &= expect(grid->itemAtPosition(1, 1) == nullptr, "cell (1,1) should remain empty");

    ok &= expect(grid->setWidgetPosition(&c, 1, 1), "moving C into an empty cell should succeed");
    ok &= expect(grid->itemAtPosition(1, 0) == nullptr, "moving C should preserve holes");
    ok &= expect(grid->itemAtPosition(1, 1) == &c, "cell (1,1) should contain moved C");

    ok &= expect(!grid->setWidgetPosition(&d, 0, 1), "placing unmanaged D onto an occupied cell should fail");
    ok &= expect(grid->setWidgetPosition(&d, 2, 0), "placing unmanaged D into a free cell should succeed");
    ok &= expect(grid->itemAtPosition(2, 0) == &d, "cell (2,0) should contain D");

    grid->insertColumn(1);
    ok &= expect(grid->itemAtPosition(0, 0) == &a, "A should stay in column 0 after insertColumn");
    ok &= expect(grid->itemAtPosition(0, 2) == &b, "B should shift to column 2 after insertColumn");

    grid->insertRow(1);
    ok &= expect(grid->itemAtPosition(2, 2) == &c, "C should shift to row 2 after insertRow");
    ok &= expect(grid->itemAtPosition(3, 0) == &d, "D should shift to row 3 after insertRow");

    SwPushButton span("Span", &root);
    ok &= expect(grid->setWidgetPosition(&span, 4, 0, 1, 2), "placing spanned item should succeed");
    ok &= expect(!grid->setWidgetPosition(&a, 4, 1), "overlapping an existing span should fail");

    if (!ok) {
        return 1;
    }

    std::cout << "[GridLayoutSelfTest] PASS\n";
    return 0;
}
