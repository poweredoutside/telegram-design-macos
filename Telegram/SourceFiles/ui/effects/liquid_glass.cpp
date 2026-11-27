#include "ui/effects/liquid_glass.h"

#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include <algorithm>
#include <cmath>

namespace Ui {
namespace {

QImage FilterBackdrop(
		const QImage &image,
		QSize size,
		int radius,
		int blur,
		int refraction) {
	if (blur <= 0 && refraction <= 0) {
		return image;
	}
	const auto width = std::max(1, size.width() / 2);
	const auto height = std::max(1, size.height() / 2);
	auto source = Images::BlurLargeImage(
		image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
		std::clamp(blur / 2, 0, std::min(width, height) - 1));
	if (source.isNull()) {
		return image;
	}
	if (refraction <= 0 || width < 3 || height < 3) {
		return source;
	}
	source = std::move(source).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	const auto halfWidth = width / 2.;
	const auto halfHeight = height / 2.;
	const auto corner = std::clamp(radius / 2., 0., std::min(halfWidth, halfHeight));
	const auto depth = std::clamp(corner * .75, 2., 12.);
	const auto band = int(std::ceil(corner + depth));
	const auto displacement = depth * std::clamp(refraction, 0, 100) / 100.;
	auto result = source.copy();
	for (auto row = 0; row != height; ++row) {
		const auto vertical = row + .5 - halfHeight;
		const auto dy = std::abs(vertical) - (halfHeight - corner);
		const auto line = reinterpret_cast<QRgb*>(result.scanLine(row));
		const auto remap = [&](int from, int to) {
			for (auto column = from; column != to; ++column) {
				const auto horizontal = column + .5 - halfWidth;
				const auto dx = std::abs(horizontal) - (halfWidth - corner);
				const auto outsideX = std::max(0., dx);
				const auto outsideY = std::max(0., dy);
				const auto outside = (outsideX && outsideY)
					? std::sqrt(outsideX * outsideX + outsideY * outsideY)
					: outsideX + outsideY;
				const auto distance = corner - outside - std::min(0., std::max(dx, dy));
				if (distance >= depth) {
					continue;
				}
				const auto edge = 1. - std::max(0., distance) / depth;
				const auto shift = displacement * edge * edge;
				const auto normalX = outside > 0. ? outsideX / outside : double(dx >= dy);
				const auto normalY = outside > 0. ? outsideY / outside : double(dx < dy);
				const auto sampleX = std::clamp(
					column - int(std::round(std::copysign(shift * normalX, horizontal))),
					0,
					width - 1);
				const auto sampleY = std::clamp(
					row - int(std::round(std::copysign(shift * normalY, vertical))),
					0,
					height - 1);
				line[column] = reinterpret_cast<const QRgb*>(
					source.constScanLine(sampleY))[sampleX];
			}
		};
		if (row < band || row >= height - band || width <= 2 * band) {
			remap(0, width);
		} else {
			remap(0, band);
			remap(width - band, width);
		}
	}
	return result;
}

} // namespace

QImage LiquidGlassBackdropCache::prepare(
		const QImage &image,
		QSize size,
		int radius,
		int blur,
		int refraction) {
	if (_size != size
		|| _radius != radius
		|| _blur != blur
		|| _refraction != refraction
		|| _source.devicePixelRatio() != image.devicePixelRatio()
		|| (_source.cacheKey() != image.cacheKey() && _source != image)) {
		_source = image;
		_size = size;
		_radius = radius;
		_blur = blur;
		_refraction = refraction;
		_filtered = FilterBackdrop(image, size, radius, blur, refraction);
	}
	return _filtered;
}

QImage CaptureLiquidGlassBackdrop(
		QWidget &source,
		QRect globalRect,
		QColor fallback) {
	const auto local = QRect(
		source.mapFromGlobal(globalRect.topLeft()),
		globalRect.size());
	const auto visible = local.intersected(source.rect());
	if (visible.isEmpty()) {
		return {};
	}
	auto sample = GrabWidgetToImage(&source, visible, fallback);
	if (local == visible) {
		return sample;
	}
	const auto ratio = sample.devicePixelRatio();
	auto result = QImage(local.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(fallback);
	auto painter = QPainter(&result);
	painter.drawImage(visible.topLeft() - local.topLeft(), sample);
	painter.end();
	return result;
}

void PaintLiquidGlass(
		QPainter &p,
		QRect rect,
		const QImage &backdrop,
		QColor tint,
		const LiquidGlassStyle &style,
		int radius,
		LiquidGlassBackdropCache *cache) {
	if (rect.isEmpty()
		|| (p.hasClipping() && !p.clipBoundingRect().intersects(rect))) {
		return;
	}
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	auto path = QPainterPath();
	path.addRoundedRect(QRectF(rect), radius, radius);
	if (!style.enabled || (style.opacity < 100 && backdrop.isNull())) {
		p.fillPath(path, tint);
		p.restore();
		return;
	}
	if (style.opacity < 100) {
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.save();
		p.setClipPath(path, Qt::IntersectClip);
		const auto filtered = cache
			? cache->prepare(backdrop, rect.size(), radius, style.blur, style.refraction)
			: FilterBackdrop(backdrop, rect.size(), radius, style.blur, style.refraction);
		p.drawImage(rect, filtered);
		p.restore();
	}
	tint.setAlphaF(std::clamp(style.opacity, 0, 100) / 100.);
	p.fillPath(path, tint);
	const auto dark = tint.lightnessF() < .5;
	const auto strength = std::clamp(style.refraction, 0, 100) / 100.;
	auto rim = QLinearGradient(rect.topLeft(), rect.bottomLeft());
	rim.setColorAt(0., QColor(255, 255, 255, int((dark ? 90 : 150) * strength) + 18));
	rim.setColorAt(.55, QColor(255, 255, 255, 10));
	rim.setColorAt(1., QColor(0, 0, 0, int((dark ? 65 : 38) * strength) + 12));
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QBrush(rim), 1.));
	p.drawRoundedRect(QRectF(rect).adjusted(.5, .5, -.5, -.5), radius, radius);
	p.restore();
}

} // namespace Ui
