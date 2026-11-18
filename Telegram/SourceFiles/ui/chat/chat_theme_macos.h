#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <QtGui/QColor>

namespace Ui {

inline constexpr auto kMacLightOutgoingGradient = std::array<std::uint32_t, 2>{
	0x5097cb, 0x4889bf,
};
inline constexpr auto kMacDarkOutgoingGradient = std::array<std::uint32_t, 2>{
	0x1a6ec2, 0x155db0,
};

[[nodiscard]] inline std::vector<QColor> MacOutgoingGradient(
		const QColor &color) {
	for (const auto &gradient : {
		kMacLightOutgoingGradient,
		kMacDarkOutgoingGradient,
	}) {
		if (color == QColor::fromRgb(gradient.front())) {
			return {
				QColor::fromRgb(gradient.front()),
				QColor::fromRgb(gradient.back()),
			};
		}
	}
	return {};
}

}
