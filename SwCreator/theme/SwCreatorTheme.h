#pragma once
/***************************************************************************************************
 * SwCreatorTheme — centralised design-token system for SwCreator.
 *
 * Every colour, font and spacing constant lives here so the whole IDE
 * speaks the same visual language.  Widgets read from the global
 * SwCreatorTheme::current() singleton instead of hard-coding rgb values.
 *
 * Identity : "Teal on Dark Slate"
 *   - Deep blue-slate surfaces with a vibrant teal accent
 *   - Warmer and more organic than VSCode (neutral blue-gray) or
 *     Qt Creator (corporate muted blue)
 **************************************************************************************************/

#include "Sw.h"
#include "SwFont.h"
#include "SwString.h"

struct SwCreatorTheme {

    // ── Surfaces ──────────────────────────────────────────────────────────
    SwColor surface0;           // deepest  (activity bar, status bar)
    SwColor surface1;           // editor / main panels
    SwColor surface2;           // sidebar, tab-bar background
    SwColor surface3;           // hover states, input backgrounds
    SwColor surface4;           // active/pressed states, borders

    // ── Brand / Accent ────────────────────────────────────────────────────
    SwColor accentPrimary;      // active indicators, primary buttons
    SwColor accentHover;        // hover variant
    SwColor accentPressed;      // pressed variant
    SwColor accentMuted;        // subtle highlights, selection backgrounds
    SwColor accentSecondary;    // info accents, secondary actions
    SwColor accentSecondaryHover;
    SwColor accentSecondaryPressed;

    // ── Text ──────────────────────────────────────────────────────────────
    SwColor textPrimary;        // main text
    SwColor textSecondary;      // labels, descriptions
    SwColor textMuted;          // disabled, placeholder
    SwColor textInverse;        // text on accent backgrounds
    SwColor textOnSurface0;     // text on deepest surface

    // ── Borders ───────────────────────────────────────────────────────────
    SwColor border;             // primary border
    SwColor borderLight;        // subtle dividers
    SwColor borderStrong;       // high-contrast borders

    // ── Semantic ──────────────────────────────────────────────────────────
    SwColor error;
    SwColor warning;
    SwColor success;
    SwColor info;

    // ── Selection / Highlight ─────────────────────────────────────────────
    SwColor selectionBg;        // selected item background
    SwColor hoverBg;            // generic hover background
    SwColor pressedBg;          // generic pressed background

    // ── Code editor ───────────────────────────────────────────────────────
    SwColor editorBg;
    SwColor editorText;
    SwColor editorGutter;
    SwColor editorLineHighlight;

    // ── Card ──────────────────────────────────────────────────────────────
    SwColor cardBg;
    SwColor cardBorder;

    // ── Workspace / Canvas ───────────────────────────────────────────────
    SwColor workspaceBg;        // darker bg behind the canvas
    SwColor canvasShadow;       // shadow colour for the elevated canvas
    SwColor canvasGridDot;      // subtle dot-grid on the canvas

    // ── Fonts ─────────────────────────────────────────────────────────────
    SwFont  codeFont;
    SwFont  uiBody;
    SwFont  uiLabel;
    SwFont  uiCaption;
    SwFont  uiHeading;
    SwFont  uiTitle;

    // ── Spacing (px) ──────────────────────────────────────────────────────
    int     spacingXs   = 4;
    int     spacingSm   = 8;
    int     spacingMd   = 12;
    int     spacingLg   = 16;
    int     spacingXl   = 24;

    int     borderRadiusSm = 2;
    int     borderRadiusMd = 2;
    int     borderRadiusLg = 2;

    int     sidebarWidth   = 48;
    int     sidebarBtnSize = 36;
    int     statusBarHeight = 24;

    // ── Helpers — build stylesheet fragments ──────────────────────────────

    static SwString rgb(const SwColor& c) {
        return "rgb(" + SwString::number(c.r) + "," + SwString::number(c.g) + ","
             + SwString::number(c.b) + ")";
    }

    SwString surfaceSS(const char* widgetType) const {
        return SwString::fromUtf8(widgetType) + " { background-color: " + rgb(surface1) + "; border-width: 0px; }";
    }

    SwString frameSS(SwColor bg, SwColor brd, int radius = 0, int borderW = 1) const {
        return "SwFrame { background-color: " + rgb(bg)
             + "; border-color: " + rgb(brd)
             + "; border-radius: " + SwString::number(radius) + "px"
             + "; border-width: " + SwString::number(borderW) + "px; }";
    }

    SwString inputSS(const char* widgetType) const {
        return SwString::fromUtf8(widgetType)
             + " { background-color: " + rgb(surface3)
             + "; border-color: " + rgb(border)
             + "; color: " + rgb(textPrimary)
             + "; border-width: 1px; border-radius: " + SwString::number(borderRadiusSm) + "px; }";
    }

    // ── Singleton ─────────────────────────────────────────────────────────

    static const SwCreatorTheme& current() {
        static SwCreatorTheme instance = darkTheme();
        return instance;
    }

    // ── Factory — dark "Teal on Dark Slate" theme ─────────────────────────

    static SwCreatorTheme darkTheme() {
        SwCreatorTheme t;

        // Surfaces
        t.surface0          = {24, 24, 28};
        t.surface1          = {30, 30, 34};
        t.surface2          = {37, 39, 44};
        t.surface3          = {45, 48, 55};
        t.surface4          = {55, 59, 68};

        // Accent — teal
        t.accentPrimary     = {0, 168, 132};
        t.accentHover       = {0, 190, 150};
        t.accentPressed     = {0, 150, 115};
        t.accentMuted       = {35, 55, 50};        // dark teal-tinted for selection
        t.accentSecondary       = {59, 130, 246};
        t.accentSecondaryHover  = {37, 99, 235};
        t.accentSecondaryPressed = {29, 78, 216};

        // Text
        t.textPrimary       = {220, 224, 232};
        t.textSecondary     = {140, 148, 162};
        t.textMuted         = {95, 102, 115};
        t.textInverse       = {255, 255, 255};
        t.textOnSurface0    = {180, 186, 196};

        // Borders
        t.border            = {55, 59, 68};
        t.borderLight       = {45, 48, 55};
        t.borderStrong      = {70, 75, 85};

        // Semantic
        t.error             = {244, 71, 71};
        t.warning           = {220, 180, 90};
        t.success           = {72, 199, 142};
        t.info              = {79, 154, 255};

        // Selection
        t.selectionBg       = {35, 55, 50};
        t.hoverBg           = {49, 55, 63};
        t.pressedBg         = {57, 64, 73};

        // Code editor
        t.editorBg          = {30, 30, 34};
        t.editorText        = {212, 212, 212};
        t.editorGutter      = {37, 39, 44};
        t.editorLineHighlight = {40, 42, 48};

        // Cards
        t.cardBg            = {37, 39, 44};
        t.cardBorder        = {55, 59, 68};

        // Workspace / Canvas
        t.workspaceBg       = {30, 30, 34};
        t.canvasShadow      = {0, 0, 0};
        t.canvasGridDot     = {50, 53, 60};

        // Fonts
        t.codeFont          = SwFont(L"Consolas", 10, Medium);
        t.uiBody            = SwFont(L"Segoe UI", 11, Normal);
        t.uiLabel           = SwFont(L"Segoe UI", 10, Medium);
        t.uiCaption         = SwFont(L"Segoe UI", 9, Normal);
        t.uiHeading         = SwFont(L"Segoe UI", 13, SemiBold);
        t.uiTitle           = SwFont(L"Segoe UI", 16, Bold);

        return t;
    }

    // ── Factory — light theme (preserves current SwCreator look) ──────────

    static SwCreatorTheme lightTheme() {
        SwCreatorTheme t;

        // Surfaces
        t.surface0          = {32, 44, 51};         // sidebar dark stays
        t.surface1          = {255, 255, 255};
        t.surface2          = {248, 250, 252};
        t.surface3          = {241, 245, 249};
        t.surface4          = {226, 232, 240};

        // Accent — same teal
        t.accentPrimary     = {0, 168, 132};
        t.accentHover       = {0, 160, 125};
        t.accentPressed     = {0, 150, 115};
        t.accentMuted       = {239, 253, 249};
        t.accentSecondary       = {59, 130, 246};
        t.accentSecondaryHover  = {37, 99, 235};
        t.accentSecondaryPressed = {29, 78, 216};

        // Text
        t.textPrimary       = {15, 23, 42};
        t.textSecondary     = {100, 116, 139};
        t.textMuted         = {148, 163, 184};
        t.textInverse       = {255, 255, 255};
        t.textOnSurface0    = {207, 212, 220};

        // Borders
        t.border            = {226, 232, 240};
        t.borderLight       = {238, 240, 241};
        t.borderStrong      = {203, 213, 225};

        // Semantic
        t.error             = {239, 68, 68};
        t.warning           = {245, 158, 11};
        t.success           = {34, 197, 94};
        t.info              = {59, 130, 246};

        // Selection
        t.selectionBg       = {219, 234, 254};
        t.hoverBg           = {248, 250, 252};
        t.pressedBg         = {241, 245, 249};

        // Code editor
        t.editorBg          = {30, 30, 30};
        t.editorText        = {220, 220, 220};
        t.editorGutter      = {37, 39, 44};
        t.editorLineHighlight = {40, 42, 48};

        // Cards
        t.cardBg            = {255, 255, 255};
        t.cardBorder        = {225, 230, 232};

        // Workspace / Canvas
        t.workspaceBg       = {235, 238, 242};
        t.canvasShadow      = {0, 0, 0};
        t.canvasGridDot     = {210, 214, 220};

        // Fonts
        t.codeFont          = SwFont(L"Consolas", 10, Medium);
        t.uiBody            = SwFont(L"Segoe UI", 11, Normal);
        t.uiLabel           = SwFont(L"Segoe UI", 10, Medium);
        t.uiCaption         = SwFont(L"Segoe UI", 9, Normal);
        t.uiHeading         = SwFont(L"Segoe UI", 13, SemiBold);
        t.uiTitle           = SwFont(L"Segoe UI", 16, Bold);

        return t;
    }
};
