#pragma once

#include <QtGui/QColor>

namespace Window::Theme {

struct LocalThemeColors {
	QColor accent;
	QColor incoming;
	QColor outgoing;
	QColor background;
	QColor wallpaper;
	QColor settings;
	QColor chats;
	QColor sidebar;
};

[[nodiscard]] LocalThemeColors CurrentLocalThemeColors();
[[nodiscard]] QColor LocalThemeTextColor(const QColor &background);
[[nodiscard]] QByteArray CollectLocalTheme(const LocalThemeColors &colors);
[[nodiscard]] bool HasThemePreview();
[[nodiscard]] bool HasLocalThemeDraft();
[[nodiscard]] bool SetLocalThemeDraft(bool local);
[[nodiscard]] int ChatBackgroundBrightness();
void SetChatBackgroundBrightness(int percent);

} // namespace Window::Theme
