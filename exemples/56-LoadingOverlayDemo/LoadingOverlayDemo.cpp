#include "SwGuiApplication.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwLoadingOverlay.h"
#include "SwMainWindow.h"
#include "SwPushButton.h"
#include "SwTimer.h"

namespace {

SwWidget* createMetricCard(const SwString& title,
                           const SwString& value,
                           const SwString& caption,
                           SwWidget* parent) {
    auto* card = new SwWidget(parent);
    card->resize(220, 126);
    card->setStyleSheet(R"(
        SwWidget {
            background-color: rgb(20, 28, 46);
            border-color: rgba(148, 163, 184, 0.18);
            border-width: 1px;
            border-radius: 18px;
        }
    )");

    auto* layout = new SwVerticalLayout(card);
    layout->setMargin(18);
    layout->setSpacing(6);

    auto* titleLabel = new SwLabel(title, card);
    titleLabel->resize(180, 18);
    titleLabel->setStyleSheet("SwLabel { color: rgb(125, 211, 252); font-size: 12px; }");

    auto* valueLabel = new SwLabel(value, card);
    valueLabel->resize(180, 34);
    valueLabel->setFont(SwFont(L"Segoe UI", 18, Bold));
    valueLabel->setStyleSheet("SwLabel { color: rgb(248, 250, 252); font-size: 25px; }");

    auto* captionLabel = new SwLabel(caption, card);
    captionLabel->resize(180, 40);
    captionLabel->setAlignment(
        DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak));
    captionLabel->setStyleSheet("SwLabel { color: rgb(148, 163, 184); font-size: 12px; }");

    layout->addWidget(titleLabel, 0, 18);
    layout->addWidget(valueLabel, 0, 34);
    layout->addWidget(captionLabel, 1, 0);
    card->setLayout(layout);
    return card;
}

SwWidget* createFeedCard(SwWidget* parent) {
    auto* card = new SwWidget(parent);
    card->resize(720, 152);
    card->setStyleSheet(R"(
        SwWidget {
            background-color: rgb(17, 24, 39);
            border-color: rgba(148, 163, 184, 0.16);
            border-width: 1px;
            border-radius: 20px;
        }
    )");

    auto* layout = new SwVerticalLayout(card);
    layout->setMargin(20);
    layout->setSpacing(8);

    auto* title = new SwLabel("Background content behind the overlay", card);
    title->resize(280, 24);
    title->setFont(SwFont(L"Segoe UI", 11, Bold));
    title->setStyleSheet("SwLabel { color: rgb(248, 250, 252); font-size: 15px; }");

    auto* line1 = new SwLabel("Route cache refreshed at 14:03:12 - 128 sessions reconciled", card);
    line1->resize(620, 22);
    line1->setStyleSheet("SwLabel { color: rgb(191, 219, 254); font-size: 12px; }");

    auto* line2 = new SwLabel("Tunnel heartbeat stable - packet loss 0.02% over the last minute", card);
    line2->resize(620, 22);
    line2->setStyleSheet("SwLabel { color: rgb(226, 232, 240); font-size: 12px; }");

    auto* line3 = new SwLabel("UI remains responsive because the overlay is scoped to this panel only", card);
    line3->resize(620, 34);
    line3->setAlignment(
        DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak));
    line3->setStyleSheet("SwLabel { color: rgb(148, 163, 184); font-size: 12px; }");

    layout->addWidget(title, 0, 24);
    layout->addWidget(line1, 0, 22);
    layout->addWidget(line2, 0, 22);
    layout->addWidget(line3, 1, 0);
    card->setLayout(layout);
    return card;
}

}  // namespace

int main() {
    SwGuiApplication app;

    SwMainWindow window(L"SwLoadingOverlay Demo", 980, 700);
    window.setStyleSheet(R"(
        SwMainWindow {
            background-color: rgb(6, 10, 18);
            color: rgb(241, 245, 249);
        }
        SwLabel {
            color: rgb(226, 232, 240);
        }
        SwPushButton {
            background-color: rgb(17, 24, 39);
            border-color: rgba(148, 163, 184, 0.24);
            color: rgb(241, 245, 249);
            border-radius: 12px;
            border-width: 1px;
            padding: 8px 16px;
            font-size: 13px;
        }
        SwPushButton:hover {
            background-color: rgb(24, 34, 56);
            border-color: rgba(125, 211, 252, 0.30);
        }
        SwPushButton:pressed {
            background-color: rgb(31, 41, 68);
            border-color: rgba(125, 211, 252, 0.40);
        }
    )");

    auto* rootLayout = new SwVerticalLayout(&window);
    rootLayout->setMargin(24);
    rootLayout->setSpacing(18);

    auto* title = new SwLabel("SwLoadingOverlay", &window);
    title->resize(420, 38);
    title->setFont(SwFont(L"Segoe UI", 24, Bold));
    title->setStyleSheet("SwLabel { color: rgb(248, 250, 252); font-size: 30px; }");

    auto* subtitle = new SwLabel(
        "Reusable loading overlay with animated spinner, semi transparent backdrop, "
        "stylesheet hooks and runtime properties.",
        &window);
    subtitle->resize(820, 42);
    subtitle->setAlignment(
        DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak));
    subtitle->setStyleSheet("SwLabel { color: rgb(148, 163, 184); font-size: 13px; }");

    auto* demoHost = new SwWidget(&window);
    demoHost->resize(900, 420);
    demoHost->setStyleSheet(R"(
        SwWidget {
            background-color: rgb(15, 23, 42);
            border-color: rgba(125, 211, 252, 0.16);
            border-width: 1px;
            border-radius: 28px;
        }
    )");

    auto* hostLayout = new SwVerticalLayout(demoHost);
    hostLayout->setMargin(24);
    hostLayout->setSpacing(16);

    auto* heroCard = new SwWidget(demoHost);
    heroCard->resize(760, 106);
    heroCard->setStyleSheet(R"(
        SwWidget {
            background-color: rgb(18, 26, 45);
            border-color: rgba(148, 163, 184, 0.16);
            border-width: 1px;
            border-radius: 20px;
        }
    )");

    auto* heroLayout = new SwVerticalLayout(heroCard);
    heroLayout->setMargin(18);
    heroLayout->setSpacing(6);

    auto* eyebrow = new SwLabel("NETWORK FABRIC", heroCard);
    eyebrow->resize(220, 18);
    eyebrow->setStyleSheet("SwLabel { color: rgb(125, 211, 252); font-size: 12px; }");

    auto* headline = new SwLabel("Loading state scoped to a single parent widget", heroCard);
    headline->resize(620, 30);
    headline->setFont(SwFont(L"Segoe UI", 16, Bold));
    headline->setStyleSheet("SwLabel { color: rgb(248, 250, 252); font-size: 21px; }");

    auto* body = new SwLabel(
        "Use the buttons below to switch theme, change metrics through properties, "
        "and replay a timed loading sequence without covering the entire window.",
        heroCard);
    body->resize(680, 40);
    body->setAlignment(
        DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak));
    body->setStyleSheet("SwLabel { color: rgb(148, 163, 184); font-size: 12px; }");

    heroLayout->addWidget(eyebrow, 0, 18);
    heroLayout->addWidget(headline, 0, 30);
    heroLayout->addWidget(body, 1, 0);
    heroCard->setLayout(heroLayout);

    auto* statsRow = new SwWidget(demoHost);
    statsRow->resize(760, 126);
    auto* statsLayout = new SwHorizontalLayout(statsRow);
    statsLayout->setMargin(0);
    statsLayout->setSpacing(16);
    statsLayout->addWidget(
        createMetricCard("Spinner", "7 styles", "Segments, rings, glass panels and ghost loaders", statsRow), 1, 0);
    statsLayout->addWidget(
        createMetricCard("Backdrop", "Semi transparent", "Content remains visible under load", statsRow), 1, 0);
    statsLayout->addWidget(
        createMetricCard("Scope", "Parent bound", "Overlay auto fills and tracks its parent", statsRow), 1, 0);
    statsRow->setLayout(statsLayout);

    hostLayout->addWidget(heroCard, 0, 106);
    hostLayout->addWidget(statsRow, 0, 126);
    hostLayout->addWidget(createFeedCard(demoHost), 1, 0);
    demoHost->setLayout(hostLayout);

    auto* statusLabel = new SwLabel("Ready. The demo opens on a sober web-style ghost ring.", &window);
    statusLabel->resize(860, 24);
    statusLabel->setStyleSheet("SwLabel { color: rgb(191, 219, 254); font-size: 13px; }");

    auto* actionBar = new SwWidget(&window);
    actionBar->resize(900, 108);
    auto* actionStack = new SwVerticalLayout(actionBar);
    actionStack->setMargin(0);
    actionStack->setSpacing(10);

    auto* actionRowTop = new SwWidget(actionBar);
    actionRowTop->resize(900, 44);
    auto* actionTopLayout = new SwHorizontalLayout(actionRowTop);
    actionTopLayout->setMargin(0);
    actionTopLayout->setSpacing(12);

    auto* actionRowBottom = new SwWidget(actionBar);
    actionRowBottom->resize(900, 44);
    auto* actionBottomLayout = new SwHorizontalLayout(actionRowBottom);
    actionBottomLayout->setMargin(0);
    actionBottomLayout->setSpacing(12);

    auto* btnSegment = new SwPushButton("Segment glow", actionRowTop);
    btnSegment->resize(148, 44);
    auto* btnSoftRing = new SwPushButton("Soft ring", actionRowTop);
    btnSoftRing->resize(148, 44);
    auto* btnMinimalRing = new SwPushButton("Minimal ring", actionRowTop);
    btnMinimalRing->resize(148, 44);
    auto* btnWarmRing = new SwPushButton("Warm ring", actionRowTop);
    btnWarmRing->resize(148, 44);

    auto* btnGlassPanel = new SwPushButton("Glass panel", actionRowBottom);
    btnGlassPanel->resize(148, 44);
    auto* btnMonoGhost = new SwPushButton("Mono ghost", actionRowBottom);
    btnMonoGhost->resize(148, 44);
    auto* btnMintGlass = new SwPushButton("Mint glass", actionRowBottom);
    btnMintGlass->resize(148, 44);
    auto* btnStop = new SwPushButton("Hide overlay", actionRowBottom);
    btnStop->resize(148, 44);

    actionTopLayout->addWidget(btnSegment, 0, 148);
    actionTopLayout->addWidget(btnSoftRing, 0, 148);
    actionTopLayout->addWidget(btnMinimalRing, 0, 148);
    actionTopLayout->addWidget(btnWarmRing, 0, 148);
    actionTopLayout->addStretch(1);
    actionRowTop->setLayout(actionTopLayout);

    actionBottomLayout->addWidget(btnGlassPanel, 0, 148);
    actionBottomLayout->addWidget(btnMonoGhost, 0, 148);
    actionBottomLayout->addWidget(btnMintGlass, 0, 148);
    actionBottomLayout->addStretch(1);
    actionBottomLayout->addWidget(btnStop, 0, 148);
    actionRowBottom->setLayout(actionBottomLayout);

    actionStack->addWidget(actionRowTop, 0, 44);
    actionStack->addWidget(actionRowBottom, 0, 44);
    actionBar->setLayout(actionStack);

    auto* footer = new SwLabel(
        "The buttons stay interactive because the overlay is attached to the demo panel, not the full window.",
        &window);
    footer->resize(880, 28);
    footer->setStyleSheet("SwLabel { color: rgb(100, 116, 139); font-size: 12px; }");

    rootLayout->addWidget(title, 0, 38);
    rootLayout->addWidget(subtitle, 0, 42);
    rootLayout->addWidget(demoHost, 1, 420);
    rootLayout->addWidget(statusLabel, 0, 24);
    rootLayout->addWidget(actionBar, 0, 108);
    rootLayout->addWidget(footer, 0, 24);
    window.setLayout(rootLayout);

    auto* overlay = new SwLoadingOverlay(demoHost);
    overlay->setBlockInput(true);
    overlay->setShowPanel(false);
    overlay->setSpinnerSize(86);
    overlay->setSpinnerThickness(7);
    overlay->setBoxWidth(240);
    overlay->setBoxMinHeight(168);
    overlay->setBoxPadding(28);
    overlay->setBoxSpacing(22);
    overlay->setSegmentLength(24);
    overlay->setSegmentCount(14);

    const char* segmentGlowStyle = R"(
        SwLoadingOverlay {
            background-color: rgba(3, 8, 18, 0.58);
            spinner-style: segments;
            spinner-color: rgb(110, 231, 255);
            spinner-trail-color: rgba(148, 163, 184, 0.10);
            spinner-glow-color: rgba(34, 211, 238, 0.38);
            color: rgb(241, 245, 249);
        }
    )";

    const char* softRingStyle = R"(
        SwLoadingOverlay {
            background-color: rgba(3, 8, 18, 0.54);
            spinner-style: ring;
            spinner-sweep: 228;
            spinner-color: rgb(125, 211, 252);
            spinner-trail-color: rgba(148, 163, 184, 0.08);
            spinner-glow-color: rgba(56, 189, 248, 0.22);
            color: rgb(226, 232, 240);
        }
    )";

    const char* minimalRingStyle = R"(
        SwLoadingOverlay {
            background-color: rgba(4, 8, 16, 0.44);
            spinner-style: minimal-ring;
            spinner-sweep: 154;
            spinner-color: rgb(229, 231, 235);
            spinner-trail-color: rgba(148, 163, 184, 0.08);
            spinner-glow-color: rgba(148, 163, 184, 0.06);
            color: rgb(203, 213, 225);
        }
    )";

    const char* warmRingStyle = R"(
        SwLoadingOverlay {
            background-color: rgba(14, 8, 4, 0.54);
            spinner-style: ring;
            spinner-sweep: 208;
            spinner-color: rgb(251, 146, 60);
            spinner-trail-color: rgba(253, 186, 116, 0.10);
            spinner-glow-color: rgba(249, 115, 22, 0.28);
            color: rgb(255, 237, 213);
        }
    )";

    const char* glassPanelStyle = R"(
        SwLoadingOverlay {
            background-color: rgba(2, 6, 23, 0.36);
            panel-background-color: rgba(15, 23, 42, 0.72);
            panel-border-color: rgba(255, 255, 255, 0.12);
            panel-shadow-color: rgba(15, 23, 42, 0.22);
            spinner-style: ring;
            spinner-sweep: 172;
            spinner-color: rgb(226, 232, 240);
            spinner-trail-color: rgba(148, 163, 184, 0.08);
            spinner-glow-color: rgba(125, 211, 252, 0.18);
            color: rgb(248, 250, 252);
            box-width: 286px;
            box-min-height: 214px;
            box-padding: 30px;
            box-spacing: 18px;
            box-radius: 32px;
        }
    )";

    const char* monoGhostStyle = R"(
        SwLoadingOverlay {
            background-color: rgba(2, 6, 23, 0.30);
            spinner-style: minimal-ring;
            spinner-sweep: 132;
            spinner-color: rgb(226, 232, 240);
            spinner-trail-color: rgba(148, 163, 184, 0.06);
            spinner-glow-color: rgba(148, 163, 184, 0.03);
            color: rgb(203, 213, 225);
        }
    )";

    const char* mintGlassStyle = R"(
        SwLoadingOverlay {
            background-color: rgba(2, 10, 8, 0.38);
            panel-background-color: rgba(6, 24, 20, 0.74);
            panel-border-color: rgba(45, 212, 191, 0.16);
            panel-shadow-color: rgba(2, 10, 8, 0.22);
            spinner-style: ring;
            spinner-sweep: 186;
            spinner-color: rgb(45, 212, 191);
            spinner-trail-color: rgba(153, 246, 228, 0.10);
            spinner-glow-color: rgba(20, 184, 166, 0.20);
            color: rgb(236, 253, 245);
            box-width: 282px;
            box-min-height: 210px;
            box-padding: 30px;
            box-spacing: 18px;
            box-radius: 32px;
        }
    )";

    auto* autoStopTimer = new SwTimer(2400, &window);
    autoStopTimer->setSingleShot(true);

    auto setIdleState = [overlay, autoStopTimer, statusLabel]() {
        autoStopTimer->stop();
        overlay->stop();
        statusLabel->setText("Overlay hidden. Replay any mode from the action bar.");
        statusLabel->setStyleSheet("SwLabel { color: rgb(134, 239, 172); font-size: 13px; }");
    };

    auto showOverlay = [overlay, autoStopTimer, statusLabel](const char* overlayText,
                                                             const char* overlayStyle,
                                                             int spinnerSize,
                                                             int thickness,
                                                             bool showPanel,
                                                             const char* statusText,
                                                             bool timed) {
        autoStopTimer->stop();
        overlay->setShowPanel(showPanel);
        overlay->setSpinnerSize(spinnerSize);
        overlay->setSpinnerThickness(thickness);
        overlay->setStyleSheet(overlayStyle);
        overlay->setText(overlayText);
        overlay->setActive(true);
        statusLabel->setText(statusText);
        statusLabel->setStyleSheet("SwLabel { color: rgb(191, 219, 254); font-size: 13px; }");
        if (timed) {
            autoStopTimer->start();
        }
    };

    SwObject::connect(autoStopTimer, &SwTimer::timeout, [setIdleState]() {
        setIdleState();
    });

    SwObject::connect(btnSegment, &SwPushButton::clicked, [showOverlay, segmentGlowStyle]() {
        showOverlay("Syncing secure tunnel routes...",
                    segmentGlowStyle,
                    72,
                    6,
                    false,
                    "Segment glow style active.",
                    false);
    });

    SwObject::connect(btnSoftRing, &SwPushButton::clicked, [showOverlay, softRingStyle]() {
        showOverlay("Loading secure session...",
                    softRingStyle,
                    78,
                    7,
                    false,
                    "Soft ring style active.",
                    false);
    });

    SwObject::connect(btnMinimalRing, &SwPushButton::clicked, [showOverlay, minimalRingStyle]() {
        showOverlay("Please wait...",
                    minimalRingStyle,
                    74,
                    5,
                    false,
                    "Minimal ring style active.",
                    false);
    });

    SwObject::connect(btnWarmRing, &SwPushButton::clicked, [showOverlay, warmRingStyle]() {
        showOverlay("Encrypting session material...",
                    warmRingStyle,
                    80,
                    7,
                    false,
                    "Warm ring style active.",
                    false);
    });

    SwObject::connect(btnGlassPanel, &SwPushButton::clicked, [showOverlay, glassPanelStyle]() {
        showOverlay("Preparing workspace...",
                    glassPanelStyle,
                    78,
                    6,
                    true,
                    "Glass panel style active.",
                    false);
    });

    SwObject::connect(btnMonoGhost, &SwPushButton::clicked, [showOverlay, monoGhostStyle]() {
        showOverlay("Fetching data...",
                    monoGhostStyle,
                    74,
                    5,
                    false,
                    "Monochrome ghost style active.",
                    false);
    });

    SwObject::connect(btnMintGlass, &SwPushButton::clicked, [showOverlay, mintGlassStyle]() {
        showOverlay("Publishing secure session...",
                    mintGlassStyle,
                    80,
                    6,
                    true,
                    "Mint glass style active.",
                    false);
    });

    SwObject::connect(btnStop, &SwPushButton::clicked, [setIdleState]() {
        setIdleState();
    });

    window.show();

    showOverlay("Please wait...",
                monoGhostStyle,
                74,
                5,
                false,
                "Initial ghost ring style active on startup. Use the action bar to compare the web presets.",
                false);

    return app.exec();
}
