#pragma once

#include "ui/userpic_view.h"

class PeerData;

namespace Ui {

void InitUserpicRounding(float64 radius, bool uniform, bool rounded);

struct UserpicRounding {
	PeerUserpicShape shape = PeerUserpicShape::Circle;
	float64 radius = .5;
};

[[nodiscard]] UserpicRounding DisplayUserpicRounding(
	const PeerData *peer,
	PeerUserpicShape requested = PeerUserpicShape::Auto);
[[nodiscard]] UserpicRounding ResolveUserpicRounding(
	PeerUserpicShape shape = PeerUserpicShape::Auto);
void PaintUserpicBackground(QPainter &p, QRect rect, UserpicRounding rounding);
[[nodiscard]] QImage ApplyUserpicShape(QImage image, UserpicRounding rounding);

void ValidateUserpicCache(
	PeerUserpicView &view,
	const QImage *cloud,
	const EmptyUserpic *empty,
	int size,
	UserpicRounding rounding);

} // namespace Ui
