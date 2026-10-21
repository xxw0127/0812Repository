#ifndef CHATBOX_CLIENT_CORE_UI_THEME_H
#define CHATBOX_CLIENT_CORE_UI_THEME_H

namespace chatbox {
namespace client {
namespace theme {

// Coinbase design language tokens (see
// docs/superpowers/specs/2026-10-19-coinbase-style-ui.md). The palette is the
// single source of truth for colours used in C++ painting and message HTML.
// resources/style.qss carries the same palette for Qt widgets; the
// themeTokensMatchStyleSheet test guards that both stay in sync.
constexpr const char* kAccent = "#0052FF";            // primary
constexpr const char* kAccentHover = "#0046DD";       // derived, no Coinbase token
constexpr const char* kAccentActive = "#003ECC";      // primary-active
constexpr const char* kCanvas = "#FFFFFF";
constexpr const char* kSurfaceSoft = "#F7F7F7";
constexpr const char* kSurfaceStrong = "#EEF0F3";
constexpr const char* kSurfaceStrongHover = "#E6E8EC";  // derived
constexpr const char* kHairline = "#DEE1E6";
constexpr const char* kInk = "#0A0B0D";
constexpr const char* kBody = "#5B616E";
constexpr const char* kMuted = "#616877";
constexpr const char* kMutedSoft = "#676D76";
constexpr const char* kOnPrimary = "#FFFFFF";
constexpr const char* kSemanticUp = "#05B169";
constexpr const char* kSemanticUpText = "#027A48";
constexpr const char* kOnlineDot = "#038A52";
constexpr const char* kSemanticDown = "#CF202F";
constexpr const char* kDangerText = "#B42318";
constexpr const char* kDisabledText = "#6B7280";
constexpr const char* kQuoteBg = "#F4F6F8";
// Deliberate deviation from the Coinbase file: green stays the semantic colour
// for the user's own messages, as a 10% tint of kSemanticUp.
constexpr const char* kBubbleOwn = "#E6F7F0";
constexpr const char* kBubbleOther = "#FFFFFF";
constexpr const char* kBannerErrorBg = "#FAE9EA";     // derived tint
constexpr const char* kBannerErrorLine = "#F2BCC0";   // derived tint

}  // namespace theme
}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_UI_THEME_H
