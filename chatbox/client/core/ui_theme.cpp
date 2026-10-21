#include "core/ui_theme.h"

#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QString>

#ifdef Q_OS_WIN
#include <QSettings>
#endif

namespace chatbox {
namespace client {
namespace theme {

namespace {

// Telegram Desktop light. Selected rows fill with the accent and switch to
// white text, which is the defining move of that UI; bubbles carry no border,
// the fill alone separates them from the canvas.
// The blue is #2C74CC rather than Telegram's #3390EC: the original gives white
// text only 3.31:1. Same hue, one step darker, 4.69:1.
const ThemePalette kLight = {
    "#2C74CC", "#2868B8", "#2461AF", "#FFFFFF", "#F4F4F5", "#F1F1F1",
    "#E6E6E6", "#E7EBF0", "#000000", "#1A1A1A", "#6A6E74", "#6B7076",
    "#FFFFFF", "#4DCD5E", "#3BA55D", "#4DCD5E", "#E53935", "#E53935",
    "#6B7076", "#F1F1F1", "#EEFFDE", "#FFFFFF", "#FFEBEB", "#FFC5C5",
    "#DADCE0", "#2C74CC", "#2868B8", "#2461AF", "#2C74CC", "#2C74CC",
    "#5F6F7A",
};

const ThemePalette kDark = {
    "#5EB5F7", "#4EA4F6", "#3390EC", "#17212B", "#0E1621", "#232E3C",
    "#2B3B4E", "#101921", "#FFFFFF", "#E4EBF5", "#708499", "#8A9AAB",
    "#FFFFFF", "#4DCD5E", "#6EE7B7", "#4DCD5E", "#FF6B6B", "#FF6B6B",
    "#6C7883", "#1B2A3A", "#2B5278", "#182533", "#3A1F22", "#5C2A2E",
    "#2B5278", "#2C74CC", "#2868B8", "#2461AF", "#2C74CC", "#2C74CC",
    "#BBD3E8",
};

}  // namespace

const char* kAccent = kLight.accent;
const char* kAccentHover = kLight.accentHover;
const char* kAccentActive = kLight.accentActive;
const char* kCanvas = kLight.canvas;
const char* kSurfaceSoft = kLight.surfaceSoft;
const char* kSurfaceStrong = kLight.surfaceStrong;
const char* kSurfaceStrongHover = kLight.surfaceStrongHover;
const char* kHairline = kLight.hairline;
const char* kInk = kLight.ink;
const char* kBody = kLight.body;
const char* kMuted = kLight.muted;
const char* kMutedSoft = kLight.mutedSoft;
const char* kOnPrimary = kLight.onPrimary;
const char* kSemanticUp = kLight.semanticUp;
const char* kSemanticUpText = kLight.semanticUpText;
const char* kOnlineDot = kLight.onlineDot;
const char* kSemanticDown = kLight.semanticDown;
const char* kDangerText = kLight.dangerText;
const char* kDisabledText = kLight.disabledText;
const char* kQuoteBg = kLight.quoteBg;
const char* kBubbleOwn = kLight.bubbleOwn;
const char* kBubbleOther = kLight.bubbleOther;
const char* kBannerErrorBg = kLight.bannerErrorBg;
const char* kBannerErrorLine = kLight.bannerErrorLine;
const char* kBubbleBorder = kLight.bubbleBorder;
const char* kPrimary = kLight.primary;
const char* kPrimaryHover = kLight.primaryHover;
const char* kPrimaryActive = kLight.primaryActive;
const char* kSelectionBg = kLight.selectionBg;
const char* kSelectionBorder = kLight.selectionBorder;
const char* kBubbleMeta = kLight.bubbleMeta;

namespace {

void assignPalette(const ThemePalette& palette) {
    kAccent = palette.accent;
    kAccentHover = palette.accentHover;
    kAccentActive = palette.accentActive;
    kCanvas = palette.canvas;
    kSurfaceSoft = palette.surfaceSoft;
    kSurfaceStrong = palette.surfaceStrong;
    kSurfaceStrongHover = palette.surfaceStrongHover;
    kHairline = palette.hairline;
    kInk = palette.ink;
    kBody = palette.body;
    kMuted = palette.muted;
    kMutedSoft = palette.mutedSoft;
    kOnPrimary = palette.onPrimary;
    kSemanticUp = palette.semanticUp;
    kSemanticUpText = palette.semanticUpText;
    kOnlineDot = palette.onlineDot;
    kSemanticDown = palette.semanticDown;
    kDangerText = palette.dangerText;
    kDisabledText = palette.disabledText;
    kQuoteBg = palette.quoteBg;
    kBubbleOwn = palette.bubbleOwn;
    kBubbleOther = palette.bubbleOther;
    kBannerErrorBg = palette.bannerErrorBg;
    kBannerErrorLine = palette.bannerErrorLine;
    kBubbleBorder = palette.bubbleBorder;
    kPrimary = palette.primary;
    kPrimaryHover = palette.primaryHover;
    kPrimaryActive = palette.primaryActive;
    kSelectionBg = palette.selectionBg;
    kSelectionBorder = palette.selectionBorder;
    kBubbleMeta = palette.bubbleMeta;
}

const ThemePalette& paletteFor(ThemeMode mode) {
    if (mode == ThemeMode::kDark ||
        (mode == ThemeMode::kSystem && systemPrefersDark())) {
        return kDark;
    }
    return kLight;
}

ThemeMode g_mode = ThemeMode::kSystem;

}  // namespace

const ThemePalette& lightPalette() {
    return kLight;
}

const ThemePalette& darkPalette() {
    return kDark;
}

ThemeMode themeMode() {
    return g_mode;
}

bool systemPrefersDark() {
#ifdef Q_OS_WIN
    QSettings personalize(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    return personalize.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
#else
    return false;
#endif
}

void setThemeMode(ThemeMode mode) {
    g_mode = mode;
    assignPalette(paletteFor(mode));
}

void applyTheme(QApplication* application, ThemeMode mode) {
    setThemeMode(mode);
    if (application == nullptr) {
        return;
    }
    const bool dark = paletteFor(mode).canvas == kDark.canvas;
    QFile style(dark ? QStringLiteral(":/style-dark.qss")
                     : QStringLiteral(":/style.qss"));
    if (style.open(QIODevice::ReadOnly | QIODevice::Text)) {
        application->setStyleSheet(QString::fromUtf8(style.readAll()));
    }
}

void applyThemeFromSettings(QApplication* application) {
    QSettings settings;
    const int stored = settings.value(QStringLiteral("settings/appearance"),
                                      static_cast<int>(ThemeMode::kSystem))
                           .toInt();
    ThemeMode mode = ThemeMode::kSystem;
    if (stored == static_cast<int>(ThemeMode::kLight)) {
        mode = ThemeMode::kLight;
    } else if (stored == static_cast<int>(ThemeMode::kDark)) {
        mode = ThemeMode::kDark;
    }
    applyTheme(application, mode);
}

}  // namespace theme
}  // namespace client
}  // namespace chatbox
