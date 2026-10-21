#ifndef CHATBOX_CLIENT_CORE_UI_THEME_H
#define CHATBOX_CLIENT_CORE_UI_THEME_H

class QApplication;

namespace chatbox {
namespace client {
namespace theme {

enum class ThemeMode {
    kSystem = 0,
    kLight = 1,
    kDark = 2,
};

struct ThemePalette {
    const char* accent;
    const char* accentHover;
    const char* accentActive;
    const char* canvas;
    const char* surfaceSoft;
    const char* surfaceStrong;
    const char* surfaceStrongHover;
    const char* hairline;
    const char* ink;
    const char* body;
    const char* muted;
    const char* mutedSoft;
    const char* onPrimary;
    const char* semanticUp;
    const char* semanticUpText;
    const char* onlineDot;
    const char* semanticDown;
    const char* dangerText;
    const char* disabledText;
    const char* quoteBg;
    const char* bubbleOwn;
    const char* bubbleOther;
    const char* bannerErrorBg;
    const char* bannerErrorLine;
};

const ThemePalette& lightPalette();
const ThemePalette& darkPalette();
ThemeMode themeMode();
bool systemPrefersDark();
void setThemeMode(ThemeMode mode);
void applyTheme(QApplication* application, ThemeMode mode);
void applyThemeFromSettings(QApplication* application);

// Current palette values. They are pointers rather than constexpr strings so
// the whole app can switch palettes at runtime without rebuilding widgets.
extern const char* kAccent;
extern const char* kAccentHover;
extern const char* kAccentActive;
extern const char* kCanvas;
extern const char* kSurfaceSoft;
extern const char* kSurfaceStrong;
extern const char* kSurfaceStrongHover;
extern const char* kHairline;
extern const char* kInk;
extern const char* kBody;
extern const char* kMuted;
extern const char* kMutedSoft;
extern const char* kOnPrimary;
extern const char* kSemanticUp;
extern const char* kSemanticUpText;
extern const char* kOnlineDot;
extern const char* kSemanticDown;
extern const char* kDangerText;
extern const char* kDisabledText;
extern const char* kQuoteBg;
extern const char* kBubbleOwn;
extern const char* kBubbleOther;
extern const char* kBannerErrorBg;
extern const char* kBannerErrorLine;

}  // namespace theme
}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_UI_THEME_H
