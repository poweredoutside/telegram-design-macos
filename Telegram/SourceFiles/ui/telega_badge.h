#pragma once

class QColor;
class QPainter;
class QRect;

namespace Api::TelegaBadges {
enum class Type;
} // namespace Api::TelegaBadges

namespace Ui {

void PaintTelegaBadge(
	QPainter &p,
	QRect area,
	Api::TelegaBadges::Type type,
	QColor color);

} // namespace Ui
