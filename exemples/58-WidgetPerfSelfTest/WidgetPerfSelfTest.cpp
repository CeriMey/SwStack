/***************************************************************************************************
 * 58-WidgetPerfSelfTest
 *
 * Stress-test et moniteur de performance du pipeline de peinture SwWidget.
 *
 * Activez le tracing détaillé avec :
 *   SW_WIDGET_PERF_TRACE=1
 *
 * Usage :
 *   WidgetPerfSelfTest [--iterations N] [--resize-only] [--no-snapshot]
 *                      [--visible-resize] [--simulate-native-live-resize]
 *                      [--paint-timeout-us N] [--pump-slice-us N]
 *
 * Phases :
 *   1  Resize storm       — N allers-retours 800×600 ↔ 1920×1080.
 *                           Mesure la cascade resizeEvent + layout recalculation.
 *   2  Paint load         — N renders off-screen via SwWidgetSnapshot.
 *                           Mesure le temps réel de paintEvent sur toute l'arborescence.
 *   3  Data churn         — N mises à jour de données (setText, setStyleSheet).
 *                           Mesure le coût des invalidations déclenchées par les setters.
 *   4  Resize + paint     — Simulation de drag-redimensionnement :
 *                           resize immédiatement suivi d'un snapshot.
 *
 ***************************************************************************************************/

// SwLabel first: it includes graphics/SwFontMetrics.h which SwComboBox.h needs
#include "SwLabel.h"
#include "SwComboBox.h"
#include "SwGuiApplication.h"
#include "SwLayout.h"
#include "SwLineEdit.h"
#include "SwMainWindow.h"
#include "SwPlainTextEdit.h"
#include "SwPushButton.h"
#include "SwScrollArea.h"
#include "SwStandardPaths.h"
#include "SwDir.h"
#include "SwTabWidget.h"
#include "SwWidget.h"
#include "SwWidgetPerfTrace.h"
#include "SwWidgetSnapshot.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ─── Bench helpers ───────────────────────────────────────────────────────────

namespace {

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::duration<double, std::milli>;

double msNow(Clock::time_point from) {
    return std::chrono::duration_cast<Ms>(Clock::now() - from).count();
}

sw::gui::perf::Metric metricFor(const char* name) {
    return sw::gui::perf::metric(name);
}

void pumpGui(SwGuiApplication& app, int durationUs) {
    if (durationUs <= 0) {
        return;
    }
    (void)app.exec(durationUs);
}

bool waitForMetricCountIncrease(SwGuiApplication& app,
                                const char* metricName,
                                unsigned long long baseline,
                                int timeoutUs,
                                int pumpSliceUs) {
    const auto deadline = Clock::now() + std::chrono::microseconds(std::max(1000, timeoutUs));
    const int sliceUs = std::max(500, pumpSliceUs);
    while (Clock::now() < deadline) {
        pumpGui(app, sliceUs);
        if (metricFor(metricName).count > baseline) {
            return true;
        }
    }
    return metricFor(metricName).count > baseline;
}

#if defined(_WIN32)
const wchar_t* kBenchWindowTitleW_ = L"SwWidget Perf Self-Test";

HWND findBenchWindow_() {
    return FindWindowW(nullptr, kBenchWindowTitleW_);
}

bool resizeVisibleWindowNative_(int clientWidth, int clientHeight) {
    HWND hwnd = findBenchWindow_();
    if (!hwnd) {
        return false;
    }

    RECT windowRect{0, 0, std::max(1, clientWidth), std::max(1, clientHeight)};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);

    return SetWindowPos(hwnd,
                        nullptr,
                        0,
                        0,
                        std::max(1, static_cast<int>(windowRect.right - windowRect.left)),
                        std::max(1, static_cast<int>(windowRect.bottom - windowRect.top)),
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
}

void setSimulatedNativeLiveResize(bool active) {
    HWND hwnd = findBenchWindow_();
    if (!hwnd) {
        return;
    }
    SendMessage(hwnd, active ? WM_ENTERSIZEMOVE : WM_EXITSIZEMOVE, 0, 0);
}
#endif

struct PhaseStat {
    std::string name;
    int         n{0};
    double      total{0.0};
    double      maxVal{0.0};

    void record(double ms) {
        total += ms;
        if (ms > maxVal) maxVal = ms;
        ++n;
    }
    double avg() const { return n > 0 ? total / n : 0.0; }
    double fps() const { return (n > 0 && total > 0) ? n * 1000.0 / total : 0.0; }

    void print() const {
        std::cout << std::fixed << std::setprecision(2)
                  << "  " << name << "\n"
                  << "    iterations : " << n                       << "\n"
                  << "    total      : " << total    << " ms\n"
                  << "    avg        : " << avg()    << " ms\n"
                  << "    max        : " << maxVal   << " ms\n"
                  << "    throughput : " << std::setprecision(1) << fps() << " /sec\n";
    }
};

// ─── UI builder ──────────────────────────────────────────────────────────────

/**
 * Builds a representative dashboard with enough widget depth to expose real
 * paint and layout overhead:
 *   - 1 tab widget with 3 tabs
 *   - Each tab holds a grid of "metric cards" (SwWidget + SwLabel children)
 *   - A status bar strip with labels and a combo
 *   - A log area (SwPlainTextEdit)
 *
 * Total widget count: ~120 widgets.
 */
struct DashboardUi {
    SwTabWidget*    tabs{nullptr};
    SwLabel*        statusLabel{nullptr};
    SwComboBox*     filterCombo{nullptr};
    SwPlainTextEdit* logView{nullptr};

    // A few leaf labels we'll setText() during data churn
    std::vector<SwLabel*> metricLabels;
};

static SwWidget* makeMetricCard(const SwString& title,
                                const SwString& value,
                                const SwString& unit,
                                SwWidget* parent,
                                DashboardUi& ui) {
    auto* card = new SwWidget(parent);
    card->resize(200, 110);
    card->setStyleSheet(R"(
        SwWidget {
            background-color: rgb(22, 32, 54);
            border-color: rgba(148, 163, 184, 0.20);
            border-width: 1px;
            border-radius: 14px;
        }
    )");

    auto* lay = new SwVerticalLayout(card);
    lay->setMargin(14);
    lay->setSpacing(4);

    auto* titleLbl = new SwLabel(title, card);
    titleLbl->resize(172, 16);
    titleLbl->setStyleSheet("SwLabel { color: rgb(125, 211, 252); font-size: 11px; }");

    auto* valueLbl = new SwLabel(value, card);
    valueLbl->resize(172, 32);
    valueLbl->setStyleSheet("SwLabel { color: rgb(248, 250, 252); font-size: 22px; }");
    ui.metricLabels.push_back(valueLbl);

    auto* unitLbl = new SwLabel(unit, card);
    unitLbl->resize(172, 16);
    unitLbl->setStyleSheet("SwLabel { color: rgb(148, 163, 184); font-size: 11px; }");

    lay->addWidget(titleLbl, 0, 16);
    lay->addWidget(valueLbl, 0, 32);
    lay->addWidget(unitLbl,  0, 16);
    card->setLayout(lay);
    return card;
}

static SwWidget* makeTabContent(const char* heading,
                                int cardCount,
                                SwWidget* parent,
                                DashboardUi& ui) {
    auto* page = new SwWidget(parent);
    page->setStyleSheet("SwWidget { background-color: rgb(12, 18, 32); }");

    auto* vlay = new SwVerticalLayout(page);
    vlay->setMargin(16);
    vlay->setSpacing(12);

    auto* hdr = new SwLabel(heading, page);
    hdr->resize(600, 24);
    hdr->setStyleSheet("SwLabel { color: rgb(248, 250, 252); font-size: 16px; }");
    vlay->addWidget(hdr, 0, 24);

    // Grid of cards (horizontal layout per row, 4 per row)
    int col = 0;
    SwHorizontalLayout* rowLay = nullptr;
    SwWidget*           rowWidget = nullptr;

    for (int i = 0; i < cardCount; ++i) {
        if (col == 0) {
            rowWidget = new SwWidget(page);
            rowWidget->resize(860, 120);
            rowLay = new SwHorizontalLayout(rowWidget);
            rowLay->setSpacing(12);
            rowWidget->setLayout(rowLay);
            vlay->addWidget(rowWidget, 0, 120);
        }

        SwString title  = SwString("Metric %1-%2").arg(heading[0]).arg(i + 1);
        SwString value  = SwString::number(1000 + i * 37);
        rowLay->addWidget(makeMetricCard(title, value, "ms", rowWidget, ui), 0, 200);
        ++col;
        if (col == 4) col = 0;
    }

    page->setLayout(vlay);
    return page;
}

static void buildDashboard(SwMainWindow& win, DashboardUi& ui) {
    // Use the managed centralWidget — SwMainWindow doesn't have setCentralWidget().
    auto* root = win.centralWidget();
    root->setStyleSheet("SwWidget { background-color: rgb(10, 15, 28); }");

    auto* rootLay = new SwVerticalLayout(root);
    rootLay->setMargin(0);
    rootLay->setSpacing(0);

    // ── Status bar strip ─────────────────────────────────────────────────────
    auto* statusStrip = new SwWidget(root);
    statusStrip->resize(win.width(), 36);
    statusStrip->setStyleSheet("SwWidget { background-color: rgb(17, 24, 39); }");
    auto* stripLay = new SwHorizontalLayout(statusStrip);
    stripLay->setMargin(8);
    stripLay->setSpacing(12);

    ui.statusLabel = new SwLabel("System nominal  |  3 nodes active  |  0 alerts", statusStrip);
    ui.statusLabel->resize(500, 20);
    ui.statusLabel->setStyleSheet("SwLabel { color: rgb(134, 239, 172); font-size: 12px; }");

    ui.filterCombo = new SwComboBox(statusStrip);
    ui.filterCombo->resize(160, 24);
    ui.filterCombo->addItem("All nodes");
    ui.filterCombo->addItem("Active only");
    ui.filterCombo->addItem("Alerts only");

    stripLay->addWidget(ui.statusLabel, 1, 0);
    stripLay->addWidget(ui.filterCombo, 0, 160);
    statusStrip->setLayout(stripLay);
    rootLay->addWidget(statusStrip, 0, 36);

    // ── Tab widget ───────────────────────────────────────────────────────────
    ui.tabs = new SwTabWidget(root);
    ui.tabs->resize(win.width(), win.height() - 36 - 120);

    ui.tabs->addTab(makeTabContent("Overview",     12, ui.tabs, ui), "Overview");
    ui.tabs->addTab(makeTabContent("Performance",  12, ui.tabs, ui), "Performance");
    ui.tabs->addTab(makeTabContent("Diagnostics",   8, ui.tabs, ui), "Diagnostics");

    rootLay->addWidget(ui.tabs, 1, 0);

    // ── Log area ─────────────────────────────────────────────────────────────
    ui.logView = new SwPlainTextEdit(root);
    ui.logView->resize(win.width(), 110);
    ui.logView->setStyleSheet(R"(
        SwPlainTextEdit {
            background-color: rgb(8, 12, 22);
            color: rgb(134, 239, 172);
            font-size: 11px;
        }
    )");
    ui.logView->setReadOnly(true);
    for (int i = 0; i < 20; ++i) {
        ui.logView->appendPlainText(
            SwString("[14:0%1:%2] Heartbeat ok — node-paris-0%3  RTT 2ms  TX 14kb  RX 8kb")
                .arg(i / 10).arg(i % 10 + 10).arg(i % 4 + 1));
    }
    rootLay->addWidget(ui.logView, 0, 110);

    root->setLayout(rootLay);
}

// ─── Data churn helpers ───────────────────────────────────────────────────────

static void churnaData(DashboardUi& ui, int variant) {
    // Rotate metric values
    for (int i = 0; i < static_cast<int>(ui.metricLabels.size()); ++i) {
        ui.metricLabels[i]->setText(SwString::number((variant * 37 + i * 13) % 9999));
    }

    // Update status text
    const char* states[] = {
        "System nominal  |  3 nodes active  |  0 alerts",
        "Refreshing...   |  2 nodes active  |  1 warning",
        "All clear       |  4 nodes active  |  0 alerts",
    };
    ui.statusLabel->setText(states[variant % 3]);

    // Append a log line
    ui.logView->appendPlainText(
        SwString("[perf] churn iteration %1").arg(variant));
}

} // namespace

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {

    int      iterations = 100;
    bool     resizeOnly = false;
    bool     noSnap     = false;
    bool     visibleResize = false;
    bool     simulateNativeLiveResize = false;
    int      paintTimeoutUs = 100000;
    int      pumpSliceUs = 2000;
    SwString snapDirArg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--resize-only") {
            resizeOnly = true;
        } else if (arg == "--no-snapshot") {
            noSnap = true;
        } else if (arg == "--visible-resize") {
            visibleResize = true;
        } else if (arg == "--simulate-native-live-resize") {
            simulateNativeLiveResize = true;
        } else if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::atoi(argv[++i]);
            if (iterations <= 0) iterations = 100;
        } else if (arg == "--paint-timeout-us" && i + 1 < argc) {
            paintTimeoutUs = std::atoi(argv[++i]);
            if (paintTimeoutUs <= 0) paintTimeoutUs = 100000;
        } else if (arg == "--pump-slice-us" && i + 1 < argc) {
            pumpSliceUs = std::atoi(argv[++i]);
            if (pumpSliceUs <= 0) pumpSliceUs = 2000;
        } else if (arg == "--snap-dir" && i + 1 < argc) {
            snapDirArg = argv[++i];
        }
    }

    // Snapshot output dir
    SwString snapDir;
    if (!noSnap) {
        if (!snapDirArg.isEmpty()) {
            snapDir = snapDirArg;
            if (!snapDir.endsWith("/") && !snapDir.endsWith("\\")) {
                snapDir += "/";
            }
        } else {
            snapDir = SwStandardPaths::writableLocation(SwStandardPaths::TempLocation)
                      + "/SwWidgetPerfSnapshots/";
        }
        if (!SwDir::mkpathAbsolute(snapDir, true)) {
            std::cerr << "[perf] Warning: cannot create snapshot dir — snapshots disabled.\n";
            noSnap = true;
        }
    }

    const bool tracing = sw::gui::perf::enabled();
    std::cout << "\n=== 58-WidgetPerfSelfTest ===\n"
              << "  iterations          : " << iterations << "\n"
              << "  visible resize      : " << (visibleResize ? "ON" : "OFF") << "\n"
              << "  native live resize  : " << (simulateNativeLiveResize ? "ON" : "OFF") << "\n"
              << "  SW_WIDGET_PERF_TRACE: " << (tracing ? "ON" : "OFF  (set =1 for detail)") << "\n\n";

    // ── Setup ────────────────────────────────────────────────────────────────
    SwGuiApplication app;

    auto t0 = Clock::now();
    SwMainWindow win("SwWidget Perf Self-Test", 1280, 820);
    DashboardUi  ui;
    buildDashboard(win, ui);
    std::cout << std::fixed << std::setprecision(2)
              << "[Setup] UI built in " << msNow(t0) << " ms  (~"
              << ui.metricLabels.size() + 50 << " widgets)\n\n";

    if (visibleResize) {
        win.show();
        pumpGui(app, 50000);
    }

    // ── Phase 1 : Resize storm ────────────────────────────────────────────────
    {
        PhaseStat stat;
        stat.name = "Phase 1 — Resize storm (800x600 <-> 1920x1080)";
        sw::gui::perf::reset();

        unsigned long long totalPaintRequests = 0;
        unsigned long long totalGeometryCalls = 0;
        unsigned long long totalGeometryInvalidations = 0;
        double totalResizeCallMs = 0.0;
        double totalPaintWaitMs = 0.0;
        int paintTimeouts = 0;

#if defined(_WIN32)
        if (visibleResize && simulateNativeLiveResize) {
            setSimulatedNativeLiveResize(true);
        }
#endif

        for (int i = 0; i < iterations; ++i) {
            const int w = (i % 2 == 0) ? 1920 : 800;
            const int h = (i % 2 == 0) ? 1080 : 600;
            const unsigned long long paintBefore = metricFor("mainWindow.paintRequest").count;
            const unsigned long long geometryBefore = metricFor("setGeometry.calls").count;
            const unsigned long long invalidateBefore = metricFor("invalidateGeometryChange.calls").count;
            auto tp = Clock::now();
            const auto resizeCallStart = Clock::now();
            bool resized = false;
#if defined(_WIN32)
            if (visibleResize) {
                resized = resizeVisibleWindowNative_(w, h);
            }
#endif
            if (!resized) {
                win.resize(w, h);
            }
            totalResizeCallMs += msNow(resizeCallStart);
            if (visibleResize && tracing) {
                const auto paintWaitStart = Clock::now();
                const bool painted =
                    waitForMetricCountIncrease(app, "mainWindow.paintRequest", paintBefore, paintTimeoutUs, pumpSliceUs);
                totalPaintWaitMs += msNow(paintWaitStart);
                if (!painted) {
                    ++paintTimeouts;
                }
            }
            stat.record(msNow(tp));
            totalPaintRequests += (metricFor("mainWindow.paintRequest").count - paintBefore);
            totalGeometryCalls += (metricFor("setGeometry.calls").count - geometryBefore);
            totalGeometryInvalidations +=
                (metricFor("invalidateGeometryChange.calls").count - invalidateBefore);
        }
        bool restored = false;
#if defined(_WIN32)
        if (visibleResize) {
            restored = resizeVisibleWindowNative_(1280, 820);
        }
#endif
        if (!restored) {
            win.resize(1280, 820);
        }
#if defined(_WIN32)
        if (visibleResize && simulateNativeLiveResize) {
            setSimulatedNativeLiveResize(false);
        }
#endif
        if (visibleResize) {
            pumpGui(app, 50000);
        }

        std::cout << "Phase 1 results:\n";
        stat.print();
        if (visibleResize && tracing) {
            std::cout << std::fixed << std::setprecision(2)
                      << "    paint/request avg : "
                      << (iterations > 0 ? static_cast<double>(totalPaintRequests) / iterations : 0.0) << "\n"
                      << "    resize call avg   : "
                      << (iterations > 0 ? totalResizeCallMs / iterations : 0.0) << " ms\n"
                      << "    paint wait avg    : "
                      << (iterations > 0 ? totalPaintWaitMs / iterations : 0.0) << " ms\n"
                      << "    setGeometry avg   : "
                      << (iterations > 0 ? static_cast<double>(totalGeometryCalls) / iterations : 0.0) << "\n"
                      << "    invalidate avg    : "
                      << (iterations > 0 ? static_cast<double>(totalGeometryInvalidations) / iterations : 0.0) << "\n"
                      << "    paint timeouts    : " << paintTimeouts << "\n";
        }
        if (tracing) {
            std::cout << "\nPhase 1 perf trace:\n";
            sw::gui::perf::dump(std::cout);
        }
        std::cout << "\n";
    }

    if (resizeOnly) {
        std::cout << "[--resize-only] Done after Phase 1.\n\n";
        return 0;
    }

    // ── Phase 2 : Paint load ──────────────────────────────────────────────────
    if (!noSnap) {
        PhaseStat stat;
        stat.name = "Phase 2 — Forced off-screen paint (SwWidgetSnapshot)";
        sw::gui::perf::reset();

        for (int i = 0; i < iterations; ++i) {
            // Switch tab so we cover all three render paths
            ui.tabs->setCurrentIndex(i % 3);
            const SwString path = snapDir + "paint_" + SwString::number(i) + ".png";
            auto tp = Clock::now();
            SwWidgetSnapshot::savePng(&win, path);
            stat.record(msNow(tp));
        }

        std::cout << "Phase 2 results:\n";
        stat.print();
        if (tracing) {
            std::cout << "\nPhase 2 perf trace:\n";
            sw::gui::perf::dump(std::cout);
        }
        std::cout << "\n";
    } else {
        std::cout << "[Phase 2] Skipped (--no-snapshot).\n\n";
    }

    // ── Phase 3 : Data churn ──────────────────────────────────────────────────
    {
        PhaseStat stat;
        stat.name = "Phase 3 — Data churn (setText / setStyleSheet)";
        sw::gui::perf::reset();

        for (int i = 0; i < iterations; ++i) {
            auto tp = Clock::now();
            churnaData(ui, i);
            stat.record(msNow(tp));
        }

        std::cout << "Phase 3 results:\n";
        stat.print();
        if (tracing) {
            std::cout << "\nPhase 3 perf trace:\n";
            sw::gui::perf::dump(std::cout);
        }
        std::cout << "\n";
    }

    // ── Phase 4 : Resize + paint (drag simulation) ───────────────────────────
    if (!noSnap) {
        PhaseStat stat;
        stat.name = "Phase 4 — Resize + immediate paint (window drag simulation)";
        sw::gui::perf::reset();

        const int steps = iterations / 2;
        for (int i = 0; i < steps; ++i) {
            const double t = static_cast<double>(i) / std::max(steps - 1, 1);
            const int w = static_cast<int>(800  + (1920 - 800)  * t);
            const int h = static_cast<int>(600  + (1080 - 600)  * t);

            auto tp = Clock::now();
            win.resize(w, h);
            const SwString path = snapDir + "drag_" + SwString::number(i) + ".png";
            SwWidgetSnapshot::savePng(&win, path);
            stat.record(msNow(tp));
        }
        win.resize(1280, 820);

        std::cout << "Phase 4 results:\n";
        stat.print();
        if (tracing) {
            std::cout << "\nPhase 4 perf trace (resize + paint combined):\n";
            sw::gui::perf::dump(std::cout);
        }
        std::cout << "\n";
    } else {
        std::cout << "[Phase 4] Skipped (--no-snapshot).\n\n";
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    std::cout << "=== Done ===\n";
    if (!tracing) {
        std::cout << "Tip: re-run with  SW_WIDGET_PERF_TRACE=1  for per-function breakdown.\n";
    }
    if (!noSnap) {
        std::cout << "Snapshots: " << snapDir.toStdString() << "\n";
    }
    std::cout << "\n";
    return 0;
}
