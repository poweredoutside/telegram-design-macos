/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_navigation.h"

#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "ui/style/style_core_scale.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/unread_counter_format.h"
#include "window/themes/window_theme_glass.h"

#include <rpl/combine.h>
#include <QtCore/QVariant>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>
#include <QtGui/QRegion>

#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

[[nodiscard]] float64 SelectionSpring(float64 delta, float64 progress) {
	constexpr auto mass = 3.;
	constexpr auto stiffness = 1000.;
	constexpr auto damping = 500.;
	const auto discriminant = std::sqrt(damping * damping - 4. * mass * stiffness);
	const auto slow = (-damping + discriminant) / (2. * mass);
	const auto fast = (-damping - discriminant) / (2. * mass);
	const auto time = .5 * progress;
	return delta * (1. - (fast * std::exp(slow * time)
		- slow * std::exp(fast * time)) / (fast - slow));
}

[[nodiscard]] QRect BarGeometry(QSize size) {
	const auto width = std::min(
		st::dialogsNavigationMaxWidth,
		std::max(0, size.width() - 2 * st::dialogsNavigationSideInset));
	return QRect(
		(size.width() - width) / 2,
		0,
		width,
		st::dialogsNavigationBarHeight);
}

[[nodiscard]] QRectF SelectionGeometry(QRect bar, float64 position) {
	const auto inset = st::dialogsNavigationInnerInset;
	const auto available = std::max(0, bar.width() - 2 * inset);
	position = std::clamp(position, 0., 3.);
	const auto lower = int(std::floor(position));
	const auto upper = std::min(lower + 1, 3);
	const auto progress = position - lower;
	const auto left = available * lower / 4;
	const auto nextLeft = available * upper / 4;
	const auto width = available * (lower + 1) / 4 - left;
	const auto nextWidth = available * (upper + 1) / 4 - nextLeft;
	return QRectF(
		bar.x() + inset + left + (nextLeft - left) * progress,
		bar.y() + inset,
		width + (nextWidth - width) * progress,
		std::max(0, bar.height() - 2 * inset));
}

class NavigationButton final : public Ui::AbstractButton {
public:
	NavigationButton(
		QWidget *parent,
		rpl::producer<QString> label,
		const style::icon &icon,
		QString animation,
		rpl::producer<bool> primary,
		rpl::producer<bool> lifted,
		rpl::producer<int> unreadCount,
		rpl::producer<> play);

protected:
	Ui::AccessibilityState accessibilityState() const override;

	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;

private:
	const style::icon &_icon;
	std::unique_ptr<Lottie::Icon> _animation;
	QString _badge;
	bool _primary = false;
	Ui::Animations::Simple _scale;
	float64 _scaleTarget = 1.;

};

NavigationButton::NavigationButton(
	QWidget *parent,
	rpl::producer<QString> label,
	const style::icon &icon,
	QString animation,
	rpl::producer<bool> primary,
	rpl::producer<bool> lifted,
	rpl::producer<int> unreadCount,
	rpl::producer<> play)
: AbstractButton(parent)
, _icon(icon)
, _animation(Lottie::MakeIcon({
	.path = std::move(animation),
	.sizeOverride = QSize(style::ConvertScale(48), style::ConvertScale(48)),
	.frame = -1,
	.colorizeUsingAlpha = true,
})) {
	std::move(primary) | rpl::on_next([=](bool active) {
		_primary = active;
		if (_animation && _animation->valid()) {
			_animation->jumpTo(_animation->framesCount() - 1, [=] { update(); });
		}
		accessibilityStateChanged({ .selected = true });
		update();
	}, lifetime());
	std::move(play) | rpl::on_next([=] {
		if (_primary && !anim::Disabled()
			&& _animation && _animation->valid()) {
			_animation->animate([=] { update(); }, 0, _animation->framesCount() - 1);
		}
	}, lifetime());
	std::move(lifted) | rpl::on_next([=](bool lifted) {
		const auto from = _scale.value(_scaleTarget);
		_scaleTarget = lifted ? 1.15 : 1.;
		_scale.start([=] { update(); }, from, _scaleTarget, 400, SelectionSpring);
	}, lifetime());
	setFocusPolicy(Qt::StrongFocus);
	setIsPageTab(true);
	std::move(unreadCount) | rpl::on_next([=](int count) {
		_badge = FormatUnreadCounterCompact(count);
		update();
	}, lifetime());
	std::move(label) | rpl::on_next([=](const QString &text) {
		setAccessibleName(text);
		setToolTip(text);
		update();
	}, lifetime());
	style::PaletteChanged() | rpl::on_next([=] {
		update();
	}, lifetime());
}

Ui::AccessibilityState NavigationButton::accessibilityState() const {
	auto result = AbstractButton::accessibilityState();
	result.selectable = true;
	result.selected = _primary;
	return result;
}

void NavigationButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);
	if (!_primary && (isOver() || isDown() || hasFocus())) {
		auto selection = st::windowFg->c;
		selection.setAlphaF(.04);
		p.setPen(Qt::NoPen);
		p.setBrush(selection);
		p.drawRoundedRect(rect(), height() / 2., height() / 2.);
	}
	const auto color = _primary
		? st::windowBgActive->c
		: st::windowFg->c;
	const auto scale = _scale.value(_scaleTarget);
	p.translate(width() / 2., height() / 2.);
	p.scale(scale, scale);
	p.translate(-width() / 2., -height() / 2.);
	if (_animation && _animation->valid()) {
		_animation->paintInCenter(p, rect(), color);
	} else {
		_icon.paintInCenter(p, rect(), color);
	}
	if (!_badge.isEmpty()) {
		const auto font = st::dialogsNavigationBadgeFont;
		const auto height = font->height + style::ConvertScale(7);
		const auto width = std::max(
			height,
			font->width(_badge) + style::ConvertScale(8));
		const auto left = std::min(
			this->width() / 2,
			this->width() - width - st::dialogsNavigationInnerInset);
		const auto badge = QRect(
			left,
			st::dialogsNavigationInnerInset,
			width,
			height);
		p.setPen(Qt::NoPen);
		p.setBrush(st::attentionButtonFg);
		p.drawRoundedRect(badge, height / 2., height / 2.);
		p.setPen(Qt::white);
		p.setFont(font);
		p.drawText(badge, Qt::AlignCenter, _badge);
	}
}

void NavigationButton::focusInEvent(QFocusEvent *e) {
	AbstractButton::focusInEvent(e);
	update();
}

void NavigationButton::focusOutEvent(QFocusEvent *e) {
	AbstractButton::focusOutEvent(e);
	update();
}

} // namespace

NavigationBar::NavigationBar(
	QWidget *parent,
	Fn<void(NavigationAction)> activate,
		Fn<QImage(QRect)> backdrop,
	Fn<void()> menu)
: RpWidget(parent)
, _activate(std::move(activate)) {
	setAttribute(Qt::WA_OpaquePaintEvent, false);
	resize(0, st::dialogsNavigationHeight);
	sizeValue() | rpl::on_next([=](QSize size) {
		const auto bar = BarGeometry(size);
		auto path = QPainterPath();
		path.addRoundedRect(QRectF(bar), bar.height() / 2., bar.height() / 2.);
		setMask(QRegion(path.toFillPolygon().toPolygon()));
	}, lifetime());
	const auto add = [&](
			NavigationAction action,
			rpl::producer<QString> label,
			const style::icon &icon,
			QString animation) {
		const auto button = Ui::CreateChild<NavigationButton>(
			this,
			std::move(label),
			icon,
			std::move(animation),
			_preview.value() | rpl::map([=](NavigationAction current) {
				return current == action;
			}),
			rpl::combine(_preview.value(), _tracking.value())
				| rpl::map([=](NavigationAction current, bool tracking) {
					return current == action && tracking && !anim::Disabled();
				}),
			_unreadCount.value() | rpl::map([=](int count) {
				return action == NavigationAction::Chats ? count : 0;
			}),
			_iconAnimations.events()
				| rpl::filter([=](NavigationAction current) {
					return current == action;
				})
				| rpl::map([] { return rpl::empty; }));
		button->setClickedCallback([=] { _activate(action); });
		button->setProperty("navigationAction", int(action));
		button->installEventFilter(this);
		if (action == NavigationAction::Settings && menu) {
			button->setContextMenuPolicy(Qt::CustomContextMenu);
			QObject::connect(button, &QWidget::customContextMenuRequested,
				button, [=] { menu(); });
		}
		sizeValue() | rpl::on_next([=](QSize size) {
			const auto bar = BarGeometry(size);
			const auto inset = st::dialogsNavigationInnerInset;
			const auto width = std::max(0, bar.width() - 2 * inset);
			const auto index = int(action);
			const auto left = bar.x() + inset + width * index / 4;
			const auto right = bar.x() + inset + width * (index + 1) / 4;
			button->resize(right - left, std::max(0, bar.height() - 2 * inset));
			button->move(left, bar.y() + inset);
		}, button->lifetime());
	};
	add(NavigationAction::Contacts, tr::lng_menu_contacts(), st::macContactsIcon,
		u":/animations/macos/tab_contacts.tgs"_q);
	add(NavigationAction::Calls, tr::lng_menu_calls(), st::macCallsIcon,
		u":/animations/macos/tab_calls.tgs"_q);
	add(NavigationAction::Chats, tr::lng_recent_chats(), st::macChatsIcon,
		u":/animations/macos/tab_chats.tgs"_q);
	add(NavigationAction::Settings, tr::lng_menu_settings(), st::macSettingsIcon,
		u":/animations/macos/tab_settings.tgs"_q);
	paintRequest() | rpl::on_next([=](QRect clip) {
		const auto bar = BarGeometry(size());
		if (!clip.intersects(bar)) {
			return;
		}
		auto p = QPainter(this);
		const auto glass = Window::Theme::GlassStyle();
		const auto background = (glass.enabled && glass.opacity < 100 && backdrop)
			? backdrop(bar)
			: QImage();
		Ui::PaintLiquidGlass(
			p,
			bar,
			background,
			st::dialogsBg->c,
			glass,
			st::dialogsNavigationBarHeight / 2,
			&_backdropCache);
		const auto selection = SelectionGeometry(
			bar,
			selectionPosition());
		auto color = st::windowFg->c;
		color.setAlphaF(st::windowBg->c.lightnessF() < .5 ? .12 : .08);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawRoundedRect(
			selection,
			selection.height() / 2.,
			selection.height() / 2.);
	}, lifetime());
	style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
	Window::Theme::GlassStyleValue() | rpl::on_next([=] { update(); }, lifetime());
}

void NavigationBar::setActive(NavigationAction action) {
	const auto from = selectionPosition();
	cancelSelection(false);
	if (_active.current() == action) {
		return;
	}
	_active = action;
	_preview = action;
	_iconAnimations.fire_copy(action);
	animateSelection(from, int(action));
	update();
}

NavigationBar::~NavigationBar() {
	cancelSelection(false);
}

void NavigationBar::animateSelection(float64 from, float64 to) {
	_selection.start(
		[=] { update(); },
		from,
		to,
		400,
		SelectionSpring);
}

float64 NavigationBar::selectionPosition() const {
	return (_gesture && _gesture->dragging)
		? _gesture->position
		: _selection.value(_gesture
			? _gesture->position
			: int(_active.current()));
}

float64 NavigationBar::positionAt(float64 x) const {
	const auto bar = BarGeometry(size());
	for (auto index = 0; index != 3; ++index) {
		const auto first = SelectionGeometry(bar, index).center().x();
		const auto second = SelectionGeometry(bar, index + 1).center().x();
		if (x < second) {
			return index + std::clamp((x - first) / std::max(1., second - first), 0., 1.);
		}
	}
	return 3.;
}

void NavigationBar::cancelSelection(bool animated) {
	if (!_gesture) {
		return;
	}
	const auto from = selectionPosition();
	const auto previewChanged = (_preview.current() != _active.current());
	_gesture.reset();
	const auto grabber = _grabber;
	_grabber = nullptr;
	if (grabber) {
		grabber->releaseMouse();
	}
	_tracking = false;
	_preview = _active.current();
	if (animated) {
		animateSelection(from, int(_active.current()));
		if (previewChanged) {
			_iconAnimations.fire_copy(_active.current());
		}
	} else {
		_selection.stop();
	}
	update();
}

bool NavigationBar::eventHook(QEvent *e) {
	if (e->type() == QEvent::Hide
		|| e->type() == QEvent::Resize
		|| e->type() == QEvent::WindowDeactivate) {
		cancelSelection(false);
	}
	return RpWidget::eventHook(e);
}

bool NavigationBar::eventFilter(QObject *watched, QEvent *e) {
	if (e->type() == QEvent::KeyPress && _gesture
		&& static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
		cancelSelection(true);
		return true;
	} else if (e->type() == QEvent::UngrabMouse && _gesture) {
		cancelSelection(true);
	} else if (e->type() == QEvent::MouseButtonPress) {
		const auto mouse = static_cast<QMouseEvent*>(e);
		if (mouse->button() == Qt::LeftButton) {
			const auto index = watched->property("navigationAction").toInt();
			const auto point = mapFromGlobal(mouse->globalPosition().toPoint());
			const auto from = selectionPosition();
			cancelSelection(false);
			_gesture = SelectionGesture{
				point, SelectionGeometry(BarGeometry(size()), index).center().x(),
				float64(index), false };
			_grabber = static_cast<QWidget*>(watched);
			_grabber->setFocus(Qt::MouseFocusReason);
			_grabber->grabMouse();
			_tracking = true;
			const auto previewChanged = (_preview.current() != NavigationAction(index));
			_preview = NavigationAction(index);
			if (previewChanged) {
				_iconAnimations.fire_copy(NavigationAction(index));
			}
			animateSelection(from, index);
			return true;
		}
	} else if (e->type() == QEvent::MouseMove && _gesture) {
		const auto mouse = static_cast<QMouseEvent*>(e);
		const auto point = mapFromGlobal(mouse->globalPosition().toPoint());
		const auto delta = point.x() - _gesture->start.x();
		if (_gesture->dragging || std::abs(delta) >= style::ConvertScale(3)) {
			_gesture->dragging = true;
			_gesture->position = positionAt(_gesture->center + delta);
			const auto action = NavigationAction(int(std::round(_gesture->position)));
			const auto previewChanged = (_preview.current() != action);
			_preview = action;
			if (previewChanged) {
				_iconAnimations.fire_copy(action);
			}
			_selection.stop();
			update();
		}
		return true;
	} else if (e->type() == QEvent::MouseButtonRelease && _gesture
		&& static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) {
		const auto mouse = static_cast<QMouseEvent*>(e);
		if (_gesture->dragging) {
			const auto delta = mapFromGlobal(mouse->globalPosition().toPoint()).x()
				- _gesture->start.x();
			_gesture->position = positionAt(_gesture->center + delta);
		}
		const auto from = selectionPosition();
		const auto action = NavigationAction(int(std::round(_gesture->position)));
		const auto play = _gesture->dragging && action != _active.current();
		_active = action;
		cancelSelection(false);
		_preview = action;
		if (play) {
			_iconAnimations.fire_copy(action);
		}
		animateSelection(from, int(action));
		_activate(action);
		return true;
	}
	return RpWidget::eventFilter(watched, e);
}

void NavigationBar::setUnreadCount(int count) {
	_unreadCount = std::max(0, count);
}

} // namespace Dialogs
