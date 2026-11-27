// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"

#include <QtGui/QImage>

class QWidget;

namespace Ui {

class BoxContent;
class LayerWidget;

struct BoxBackground {
	Fn<bool()> enabled;
	Fn<QImage(QColor, int)> image;
};

void SetBoxBackgroundFactory(Fn<BoxBackground(not_null<LayerWidget*>)> factory);
void AllowBoxBackgroundWithScroll(not_null<BoxContent*> box);
[[nodiscard]] bool BoxUsesCustomBackground(not_null<const QWidget*> child);

} // namespace Ui
