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

const ThemePalette kLight = {
    "#0052FF", "#0046DD", "#003ECC", "#FFFFFF", "#F7F7F7", "#EEF0F3",
    "#E6E8EC", "#DEE1E6", "#0A0B0D", "#5B616E", "#616877", "#676D76",
    "#FFFFFF", "#05B169", "#027A48", "#038A52", "#CF202F", "#B42318",
    "#6B7280", "#F4F6F8", "#E6F7F0", "#FFFFFF", "#FAE9EA", "#F2BCC0",
    "#7C848E", "#0052FF", "#0046DD", "#003ECC", "#CCDCFF", "#A9C1F0",
};

const ThemePalette kDark = {
    "#4D8DFF", "#3B7BEB", "#2F6FDC", "#0F1419", "#1A2027", "#232B33",
    "#2B343D", "#323B44", "#F2F4F7", "#AEB6C0", "#8A929C", "#9AA2AC",
    "#FFFFFF", "#34D399", "#6EE7B7", "#34D399", "#FF7A7A", "#FF8A80",
    "#8A929C", "#202830", "#14332A", "#1A2027", "#3A2024", "#6B3035",
    "#667380", "#2F6FDC", "#2660C7", "#1F54B0", "#2A3E5C", "#3D6299",
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
