#pragma once

#include <QtGui/QImage>

namespace Window {

[[nodiscard]] QImage ReadCustomApplicationIcon(const QString &path);
[[nodiscard]] QImage SavedCustomApplicationIcon();
[[nodiscard]] bool CustomApplicationIconSelected();
[[nodiscard]] bool SaveCustomApplicationIcon(const QImage &image);
void SelectCustomApplicationIcon(bool selected);
void InitCustomApplicationIcon();
[[nodiscard]] const QImage &ActiveCustomApplicationIcon();

} // namespace Window
