/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_common.h"

#include "base/platform/base_platform_info.h"
#include "base/timer.h"
#include "lottie/lottie_icon.h"
#include "menu/menu_send_details.h"
#include "settings/settings_key_navigation.h"
#include "ui/effects/animations.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/search_field_controller.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text/text_entity.h"
#include "ui/style/style_core_scale.h"
#include "styles/style_edit_peer_members.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QAction>
#include <QtGui/QPainterPath>
#include <array>

namespace Settings {
namespace {

class GroupedSettingsContent final : public Ui::RpWidget {
public:
	explicit GroupedSettingsContent(QWidget *parent);
	[[nodiscard]] not_null<Ui::VerticalLayout*> content() const;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	const not_null<Ui::VerticalLayout*> _content;

};

GroupedSettingsContent::GroupedSettingsContent(QWidget *parent)
: RpWidget(parent)
, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	_content->heightValue() | rpl::on_next([=](int height) {
		resize(width(), height);
	}, lifetime());
}

not_null<Ui::VerticalLayout*> GroupedSettingsContent::content() const {
	return _content;
}

int GroupedSettingsContent::resizeGetHeight(int newWidth) {
	const auto innerWidth = std::clamp(
		newWidth - 2 * st::macSettingsPageInset,
		0,
		style::ConvertScale(600));
	_content->resizeToWidth(innerWidth);
	_content->move((newWidth - innerWidth) / 2, 0);
	return _content->height();
}

const auto &MacButtonStyles() {
	static const auto Styles = std::array{
		std::pair{ &st::settingsButton, &st::macSettingsPageButton },
		std::pair{ &st::settingsButtonNoIcon, &st::macSettingsPageButtonNoIcon },
		std::pair{ &st::settingsButtonLight, &st::macSettingsPageButtonLight },
		std::pair{ &st::settingsButtonLightNoIcon, &st::macSettingsPageButtonLightNoIcon },
		std::pair{ &st::settingsButtonActive, &st::macSettingsPageButtonActive },
		std::pair{ &st::settingsAttentionButton, &st::macSettingsPageAttentionButton },
		std::pair{ &st::settingsAttentionButtonWithIcon, &st::macSettingsPageAttentionButtonWithIcon },
		std::pair{ &st::settingsButtonRightLabelSpoiler, &st::macSettingsPageButtonRightLabelSpoiler },
		std::pair{ &st::settingsButtonNoIconLocked, &st::macSettingsPageButtonNoIconLocked },
		std::pair{ &st::settingsButtonDisabledWithIcon, &st::macSettingsPageButtonDisabled },
		std::pair{ &st::settingsOptionDisabled, &st::macSettingsPageOptionDisabled },
	};
	return Styles;
}

bool IsMacButtonStyle(const style::SettingsButton &st) {
	return ranges::any_of(MacButtonStyles(), [&](const auto &pair) {
		return pair.second == &st;
	});
}

[[nodiscard]] HighlightArgs MaybeFillFromRipple(
		not_null<QWidget*> target,
		HighlightArgs args) {
	if (!args.rippleShape) {
		return args;
	}
	const auto widget = target.get();
	if (const auto icon = dynamic_cast<Ui::IconButton*>(widget)) {
		const auto &st = icon->st();
		args.shape = HighlightShape::Ellipse;
		args.margin = QMargins(
			st.rippleAreaPosition.x(),
			st.rippleAreaPosition.y(),
			st.width - st.rippleAreaPosition.x() - st.rippleAreaSize,
			st.height - st.rippleAreaPosition.y() - st.rippleAreaSize);
	} else if (const auto round = dynamic_cast<Ui::RoundButton*>(widget)) {
		const auto &st = round->st();
		args.shape = HighlightShape::Rect;
		args.radius = st.radius ? st.radius : st::buttonRadius;
	}
	return args;
}

class HighlightOverlay final : public Ui::RpWidget {
public:
	HighlightOverlay(not_null<QWidget*> target, HighlightArgs &&args);

protected:
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	enum class Phase {
		Wait,
		FadeIn,
		Shown,
		FadeOut,
	};

	void updateGeometryFromTarget();
	void updateZOrder();
	void startFadeIn();
	void startFadeOut();
	void nextPhase();
	void nextPhaseIn(crl::time delay);
	void finish();

	const QPointer<QWidget> _target;
	const HighlightArgs _args;
	const style::color &_color;

	Ui::Animations::Simple _animation;
	base::Timer _phaseTimer;
	Phase _phase = Phase::Wait;
	bool _finishing = false;

};

HighlightOverlay::HighlightOverlay(
	not_null<QWidget*> target,
	HighlightArgs &&args)
: RpWidget(target->parentWidget())
, _target(target.get())
, _args(MaybeFillFromRipple(target, std::move(args)))
, _color(_args.color ? *_args.color : st::windowBgActive)
, _phaseTimer([=] { nextPhase(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);

	target->installEventFilter(this);

	QObject::connect(
		target.get(),
		&QObject::destroyed,
		this,
		[this] { finish(); });

	updateGeometryFromTarget();
	updateZOrder();

	paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(this);

		const auto progress = _animation.value(
			(_phase == Phase::Wait || _phase == Phase::FadeOut) ? 0. : 1.);
		const auto alpha = _args.opacity * progress;

		auto color = _color->c;
		color.setAlphaF(color.alphaF() * alpha);

		p.setPen(Qt::NoPen);
		p.setBrush(color);

		const auto r = rect();
		switch (_args.shape) {
		case HighlightShape::Rect:
			if (_args.radius > 0) {
				PainterHighQualityEnabler hq(p);
				p.drawRoundedRect(r, _args.radius, _args.radius);
			} else {
				p.drawRect(r);
			}
			break;
		case HighlightShape::Ellipse:
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(r);
			break;
		}
	}, lifetime());

	hide();
	nextPhaseIn(_args.showDelay);
}

bool HighlightOverlay::eventFilter(QObject *o, QEvent *e) {
	if (o != _target.data()) {
		return false;
	}
	switch (e->type()) {
	case QEvent::Move:
	case QEvent::Resize:
		updateGeometryFromTarget();
		break;
	case QEvent::ZOrderChange:
		updateZOrder();
		break;
	case QEvent::Show:
	case QEvent::Hide:
		setVisible(!_target->isHidden());
		break;
	default:
		break;
	}
	return false;
}

void HighlightOverlay::updateGeometryFromTarget() {
	if (_target) {
		setGeometry(_target->geometry().marginsRemoved(_args.margin));
	}
}

void HighlightOverlay::updateZOrder() {
	if (!_target) {
		return;
	}
	if (_args.below) {
		stackUnder(_target);
	} else {
		const auto parent = _target->parentWidget();
		const auto siblings = parent ? parent->children() : QObjectList();
		const auto it = ranges::find(siblings, _target.data());
		const auto next = (it != siblings.end()) ? (it + 1) : it;
		if (next != siblings.end()) {
			if (const auto widget = qobject_cast<QWidget*>(*next)) {
				stackUnder(widget);
				return;
			}
		}
		raise();
	}
}

void HighlightOverlay::startFadeIn() {
	show();

	_phase = Phase::FadeIn;
	_animation.start([=] {
		update();
		if (!_animation.animating()) {
			_phase = Phase::Shown;
			nextPhaseIn(_args.shownDuration);
		}
	}, 0., 1., _args.showDuration, anim::easeOutCubic);
}

void HighlightOverlay::startFadeOut() {
	_phase = Phase::FadeOut;
	_animation.start([=] {
		update();
		if (!_animation.animating()) {
			finish();
		}
	}, 1., 0., _args.hideDuration, anim::easeInCubic);
}

void HighlightOverlay::nextPhase() {
	switch (_phase) {
	case Phase::Wait:
		startFadeIn();
		break;
	case Phase::Shown:
		startFadeOut();
		break;
	default:
		Unexpected("Next phase called during Fade phase.");
	}
}

void HighlightOverlay::nextPhaseIn(crl::time delay) {
	if (!delay) {
		_phaseTimer.cancel();
		nextPhase();
	} else {
		_phaseTimer.callOnce(delay);
	}
}

void HighlightOverlay::finish() {
	if (_finishing) {
		return;
	}
	_finishing = true;
	_phaseTimer.cancel();
	_animation.stop();
	deleteLater();
}

} // namespace

void HighlightWidget(QWidget *target, HighlightArgs &&args) {
	if (!target) {
		return;
	}
	if (args.scroll) {
		ScrollToWidget(target);
	}
	for (auto parent = target->parentWidget()
		; parent
		; parent = parent->parentWidget()) {
		if (const auto section = dynamic_cast<AbstractSection*>(parent)) {
			section->setNavigationAnchor(target);
			break;
		}
	}
	new HighlightOverlay(target, std::move(args));
}

void ScrollToWidget(not_null<QWidget*> target) {
	const auto scrollIn = [&](auto &&scroll) {
		if (const auto inner = scroll->widget()) {
			const auto globalPosition = target->mapToGlobal(QPoint(0, 0));
			const auto localPosition = inner->mapFromGlobal(globalPosition);
			const auto localTop = localPosition.y();
			const auto targetHeight = target->height();
			const auto scrollHeight = scroll->height();
			const auto centered = localTop - (scrollHeight - targetHeight) / 2;
			const auto top = std::clamp(centered, 0, scroll->scrollTopMax());
			scroll->scrollToY(top);
		}
	};
	for (auto parent = target->parentWidget()
		; parent
		; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(parent)) {
			scrollIn(scroll);
			return;
		}
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			scrollIn(scroll);
			return;
		}
	}
}

void RevealWidget(not_null<QWidget*> target, int margin) {
	const auto scrollIn = [&](auto &&scroll) {
		if (const auto inner = scroll->widget()) {
			const auto globalPosition = target->mapToGlobal(QPoint(0, 0));
			const auto localTop = inner->mapFromGlobal(globalPosition).y();
			scroll->scrollToY(
				localTop - margin,
				localTop + target->height() + margin);
		}
	};
	for (auto parent = target->parentWidget()
		; parent
		; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(parent)) {
			scrollIn(scroll);
			return;
		}
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			scrollIn(scroll);
			return;
		}
	}
}

HighlightArgs SubsectionTitleHighlight() {
	const auto radius = st::roundRadiusSmall;
	return { .margin = { -radius, 0, -radius, 0 }, .radius = radius };
}

AbstractSection::AbstractSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: _controller(controller) {
}

AbstractSection::~AbstractSection() = default;

SendMenu::Details AbstractSection::sendMenuDetails() const {
	return {};
}

bool AbstractSection::processChosenSticker(ChatHelpers::FileChosen &&) {
	return false;
}

void AbstractSection::build(
		not_null<Ui::VerticalLayout*> container,
		SectionBuildMethod method) {
	method(
		container,
		_controller,
		showOtherMethod(),
		_showFinished.events());
}

void AbstractSection::keyPressEvent(QKeyEvent *e) {
	if (!_keyNavigation) {
		_keyNavigation = std::make_unique<KeyNavigation>(this);
	}
	if (!_keyNavigation->handle(e)) {
		RpWidget::keyPressEvent(e);
	}
}

void AbstractSection::setNavigationAnchor(not_null<QWidget*> widget) {
	if (!_keyNavigation) {
		_keyNavigation = std::make_unique<KeyNavigation>(this);
	}
	_keyNavigation->anchorTo(widget);
}

Icon::Icon(IconDescriptor descriptor) : _icon(descriptor.icon) {
	const auto background = [&]() -> const style::color* {
		if (descriptor.type == IconType::Simple) {
			return nullptr;
		}
		return descriptor.background;
	}();
	if (background) {
		const auto radius = (descriptor.type == IconType::Rounded)
			? st::settingsIconRadius
			: (std::min(_icon->width(), _icon->height()) / 2);
		_background.emplace(radius, *background);
	} else if (const auto brush = descriptor.backgroundBrush) {
		const auto radius = (descriptor.type == IconType::Rounded)
			? st::settingsIconRadius
			: (std::min(_icon->width(), _icon->height()) / 2);
		_backgroundBrush.emplace(radius, std::move(*brush));
	}
}

void Icon::paint(QPainter &p, QPoint position) const {
	paint(p, position.x(), position.y());
}

void Icon::paint(QPainter &p, int x, int y) const {
	if (_background) {
		_background->paint(p, { { x, y }, _icon->size() });
	} else if (_backgroundBrush) {
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(_backgroundBrush->second);
		p.drawRoundedRect(
			QRect(QPoint(x, y), _icon->size()),
			_backgroundBrush->first,
			_backgroundBrush->first);
	}
	_icon->paint(p, { x, y }, 2 * x + _icon->width());
}

int Icon::width() const {
	return _icon->width();
}

int Icon::height() const {
	return _icon->height();
}

QSize Icon::size() const {
	return _icon->size();
}

not_null<Ui::VerticalLayout*> AddGroupedSettingsContent(
		not_null<Ui::VerticalLayout*> container) {
	return container->add(object_ptr<GroupedSettingsContent>(container))->content();
}

bool IsGroupedSettingsContent(not_null<QWidget*> widget) {
	for (auto ancestor = widget.get(); ancestor; ancestor = ancestor->parentWidget()) {
		if (dynamic_cast<GroupedSettingsContent*>(ancestor)) {
			return true;
		}
	}
	return false;
}

not_null<Ui::VerticalLayout*> AddGroupedSettingsRows(
		not_null<Ui::VerticalLayout*> container) {
	return container->add(object_ptr<Ui::VerticalLayout>(container));
}

void FinishGroupedSettingsRows(not_null<Ui::VerticalLayout*> group) {
	const auto overlay = Ui::CreateChild<Ui::RpWidget>(group);
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	group->sizeValue() | rpl::on_next([=](QSize size) {
		overlay->setGeometry(QRect(QPoint(), size));
		overlay->raise();
		overlay->update();
	}, overlay->lifetime());
	style::PaletteChanged() | rpl::on_next([=] {
		overlay->update();
	}, overlay->lifetime());
	overlay->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(overlay);
		const auto plain = (st::boxSearchBg->c == st::windowBg->c);
		auto rows = std::vector<not_null<Ui::RpWidget*>>();
		for (auto i = 0; i != group->count(); ++i) {
			const auto row = group->widgetAt(i);
			if (row->height() > 0) {
				rows.push_back(row);
			}
		}
		const auto inset = style::ConvertScale(14);
		for (auto i = 0; i != int(rows.size()); ++i) {
			if (plain || i + 1 != rows.size()) {
				p.fillRect(inset,
					rows[i]->y() + rows[i]->height() - st::lineWidth,
					std::max(0, group->width() - 2 * inset),
					st::lineWidth, st::shadowFg);
			}
		}
		if (!plain && !rows.empty()) {
			p.setRenderHint(QPainter::Antialiasing);
			auto outside = QPainterPath();
			outside.setFillRule(Qt::OddEvenFill);
			outside.addRect(overlay->rect());
			const auto radius = std::min(
				st::macSettingsRadius, overlay->height() / 2);
			outside.addRoundedRect(overlay->rect(), radius, radius);
			p.fillPath(outside, st::boxSearchBg);
		}
	}, overlay->lifetime());
	for (auto i = 0; i != group->count(); ++i) {
		group->widgetAt(i)->geometryValue() | rpl::on_next([=] {
			overlay->raise();
			overlay->update();
		}, overlay->lifetime());
	}
	overlay->raise();
	overlay->show();
}

not_null<Ui::FlatLabel*> AddGroupedSettingsHeader(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text) {
	return container->add(object_ptr<Ui::FlatLabel>(
		container, std::move(text), st::macSettingsPageHeader),
		style::margins(style::ConvertScale(14), style::ConvertScale(20),
			style::ConvertScale(14), style::ConvertScale(5)));
}

void AddGroupedSettingsFooter(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text) {
	container->add(object_ptr<Ui::FlatLabel>(
		container, std::move(text), st::macSettingsPageHeader),
		style::margins(style::ConvertScale(14), style::ConvertScale(5),
			style::ConvertScale(14), 0));
}

const style::SettingsButton &ResolveButtonStyle(
		not_null<QWidget*> parent,
		const style::SettingsButton &st) {
	if (!Platform::IsWindows()) {
		return st;
	}
	for (auto ancestor = parent.get(); ancestor; ancestor = ancestor->parentWidget()) {
		if (dynamic_cast<AbstractSection*>(ancestor)) {
			for (const auto &[original, replacement] : MacButtonStyles()) {
				if (original == &st) {
					return *replacement;
				}
			}
			break;
		}
	}
	return st;
}

void AddButtonIcon(
		not_null<Ui::AbstractButton*> button,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	Expects(descriptor.icon != nullptr);

	struct IconWidget {
		IconWidget(QWidget *parent, IconDescriptor &&descriptor)
		: widget(parent)
		, icon(std::move(descriptor)) {
		}
		Ui::RpWidget widget;
		Icon icon;
	};
	const auto icon = button->lifetime().make_state<IconWidget>(
		button,
		std::move(descriptor));
	icon->widget.setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->widget.resize(icon->icon.size());
	icon->widget.show();
	button->sizeValue(
	) | rpl::on_next([=, left = st.iconLeft](QSize size) {
		icon->widget.moveToLeft(
			left,
			(size.height() - icon->widget.height()) / 2,
			size.width());
	}, icon->widget.lifetime());
	icon->widget.paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(&icon->widget);
		icon->icon.paint(p, 0, 0);
	}, icon->widget.lifetime());
}

object_ptr<Button> CreateButtonWithIcon(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		const style::SettingsButton &original,
		IconDescriptor &&descriptor) {
	const auto &st = ResolveButtonStyle(parent, original);
	auto result = object_ptr<Button>(parent, std::move(text), st);
	const auto button = result.data();
	button->setAllowTextLines();
	if (descriptor) {
		AddButtonIcon(button, st, std::move(descriptor));
	}
	return result;
}

not_null<Button*> AddButtonWithIcon(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	auto button = CreateButtonWithIcon(
		container,
		std::move(text),
		st,
		std::move(descriptor));
	const auto inset = IsMacButtonStyle(button->st())
		&& !IsGroupedSettingsContent(container)
		? st::macSettingsPageInset
		: 0;
	return container->add(std::move(button), { inset, 0, inset, 0 });
}

void CreateRightLabel(
		not_null<Button*> button,
		v::text::data &&label,
		const style::SettingsButton &original,
		rpl::producer<QString> buttonText,
		Ui::Text::MarkedContext context) {
	const auto &st = ResolveButtonStyle(button, original);
	const auto rightSkip = IsMacButtonStyle(st)
		? st.padding.right()
		: st::settingsButtonRightSkip;
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		st.rightLabel);
	name->show();
	const auto layout = [=, &st](const QString &buttonText) {
		const auto full = button->width()
			- st.padding.left()
			- st.padding.right()
			- rightSkip;
		const auto textWidth = st.style.font->width(buttonText);
		const auto natural = name->textMaxWidth();
		auto widestWord = 0;
		for (const auto &word : buttonText.split(' ', Qt::SkipEmptyParts)) {
			widestWord = std::max(widestWord, st.style.font->width(word));
		}

		// WHY: the value is never cut for a long label: the label wraps
		// instead (when the button allows it), keeping its widest word and
		// a third of the row, or the value moves under it when even that fails.
		const auto labelMin = std::max(widestWord, full / 3);
		const auto sideBySide = (textWidth + natural <= full)
			|| (natural <= full - labelMin);
		name->resizeToNaturalWidth(sideBySide
			? natural
			: (full + st.padding.right()));
		auto padding = st.padding;
		if (sideBySide) {
			padding.setRight(padding.right()
				+ rightSkip
				+ name->width());
		} else {
			padding.setBottom(padding.bottom() + name->height());
		}
		button->setPaddingOverride(padding);
		const auto single = st.padding.top()
			+ st.height
			+ st.padding.bottom();
		name->moveToRight(
			rightSkip,
			(sideBySide
				? (st.padding.top() + (button->height() - single) / 2)
				: (button->height() - st.padding.bottom() - name->height())));
	};
	if (v::text::is_plain(label)) {
		rpl::combine(
			button->sizeValue(),
			std::move(buttonText),
			v::text::take_plain(std::move(label))
		) | rpl::on_next([=](
				QSize,
				const QString &button,
				const QString &text) {
			name->setText(text);
			layout(button);
		}, name->lifetime());
	} else if (v::text::is_marked(label)) {
		rpl::combine(
			button->sizeValue(),
			std::move(buttonText),
			v::text::take_marked(std::move(label))
		) | rpl::on_next([=](
				QSize,
				const QString &button,
				const TextWithEntities &text) {
			name->setMarkedText(text, context);
			layout(button);
		}, name->lifetime());
	}
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
}

SeparatedToggle AddSeparatedToggle(
		not_null<Button*> button,
		const style::SettingsButton &st,
		bool checked) {
	const auto container = button->parentWidget();
	const auto toggle = Ui::CreateChild<Button>(container, nullptr, st);
	const auto checkView = button->lifetime().make_state<Ui::ToggleView>(
		st.toggle,
		checked,
		[=] { toggle->update(); });

	const auto separator = Ui::CreateChild<Ui::RpWidget>(container);
	separator->paintRequest(
	) | rpl::on_next([=, bg = st.textBgOver] {
		auto p = QPainter(separator);
		p.fillRect(separator->rect(), bg);
	}, separator->lifetime());
	const auto separatorHeight = 2 * st.toggle.border + st.toggle.diameter;
	button->geometryValue(
	) | rpl::on_next([=](const QRect &r) {
		const auto width = st::rightsButtonToggleWidth;
		toggle->setGeometry(
			r.x() + r.width() - width,
			r.y(),
			width,
			r.height());
		separator->setGeometry(
			toggle->x() - st::lineWidth,
			r.y() + (r.height() - separatorHeight) / 2,
			st::lineWidth,
			separatorHeight);
	}, toggle->lifetime());

	const auto checkWidget = Ui::CreateChild<Ui::RpWidget>(toggle);
	checkWidget->resize(checkView->getSize());
	checkWidget->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(checkWidget);
		checkView->paint(p, 0, 0, checkWidget->width());
	}, checkWidget->lifetime());
	toggle->sizeValue(
	) | rpl::on_next([=, &st](const QSize &s) {
		checkWidget->moveToRight(
			st.toggleSkip,
			(s.height() - checkWidget->height()) / 2);
	}, toggle->lifetime());

	separator->show();
	checkWidget->show();
	toggle->show();

	return { toggle, checkView };
}

not_null<Button*> AddButtonWithLabel(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		rpl::producer<QString> label,
		const style::SettingsButton &st,
		IconDescriptor &&descriptor) {
	const auto button = AddButtonWithIcon(
		container,
		rpl::duplicate(text),
		st,
		std::move(descriptor));
	CreateRightLabel(button, std::move(label), st, std::move(text));
	return button;
}

void AddDividerTextWithLottie(
		not_null<Ui::VerticalLayout*> container,
		DividerWithLottieDescriptor &&descriptor) {
	const auto divider = Ui::CreateChild<Ui::BoxContentDivider>(
		container.get(),
		0,
		st::defaultDividerBar,
		descriptor.parts);
	const auto verticalLayout = container->add(
		object_ptr<Ui::VerticalLayout>(container.get()));
	const auto size = descriptor.lottieSize.value_or(
		st::settingsFilterIconSize);
	auto icon = CreateLottieIcon(
		verticalLayout,
		{
			.name = descriptor.lottie,
			.sizeOverride = { size, size },
		},
		descriptor.lottieMargins.value_or(st::settingsFilterIconPadding));
	if (descriptor.showFinished) {
		const auto repeat = descriptor.lottieRepeat.value_or(
			anim::repeat::once);
		std::move(
			descriptor.showFinished
		) | rpl::on_next([animate = std::move(icon.animate), repeat] {
			animate(repeat);
		}, verticalLayout->lifetime());
	}
	verticalLayout->add(std::move(icon.widget));

	if (descriptor.about) {
		verticalLayout->add(
			object_ptr<Ui::FlatLabel>(
				verticalLayout,
				std::move(descriptor.about),
				st::settingsFilterDividerLabel),
			descriptor.aboutMargins.value_or(
				st::settingsFilterDividerLabelPadding),
			style::al_top)->setTryMakeSimilarLines(true);
	}

	verticalLayout->geometryValue(
	) | rpl::on_next([=](const QRect &r) {
		divider->setGeometry(r);
	}, divider->lifetime());
}

LottieIcon CreateLottieIcon(
		not_null<QWidget*> parent,
		Lottie::IconDescriptor &&descriptor,
		style::margins padding,
		Fn<QColor()> colorOverride) {
	Expects(!descriptor.frame); // I'm not sure it considers limitFps.

	descriptor.limitFps = true;

	auto object = object_ptr<Ui::RpWidget>(parent);
	const auto raw = object.data();

	const auto width = descriptor.sizeOverride.width();
	raw->resize(QRect(
		QPoint(),
		descriptor.sizeOverride).marginsAdded(padding).size());

	auto owned = Lottie::MakeIcon(std::move(descriptor));
	const auto icon = owned.get();

	raw->lifetime().add([kept = std::move(owned)]{});
	const auto looped = raw->lifetime().make_state<bool>(true);

	const auto start = [=] {
		icon->animate([=] {
			raw->update();
		}, 0, icon->framesCount() - 1);
	};
	const auto animate = [=](anim::repeat repeat) {
		*looped = (repeat == anim::repeat::loop);
		start();
	};
	raw->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(raw);
		const auto left = (raw->width() - width) / 2;
		icon->paint(p, left, padding.top(), colorOverride
			? colorOverride()
			: std::optional<QColor>());
		if (!icon->animating() && icon->frameIndex() > 0 && *looped) {
			start();
		}

	}, raw->lifetime());

	return {
		.widget = std::move(object),
		.animate = std::move(animate),
		.animating = [=] { return icon->animating(); },
	};
}

SliderWithLabel MakeSliderWithLabel(
		QWidget *parent,
		const style::MediaSlider &sliderSt,
		const style::FlatLabel &labelSt,
		int skip,
		int minLabelWidth,
		bool ignoreWheel) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();
	const auto height = std::max(
		sliderSt.seekSize.height(),
		labelSt.style.font->height);
	raw->resize(sliderSt.seekSize.width(), height);
	const auto slider = ignoreWheel
		? Ui::CreateChild<Ui::MediaSliderWheelless>(raw, sliderSt)
		: Ui::CreateChild<Ui::MediaSlider>(raw, sliderSt);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(raw, labelSt);
	slider->resize(slider->width(), sliderSt.seekSize.height());
	rpl::combine(
		raw->sizeValue(),
		label->sizeValue()
	) | rpl::on_next([=](QSize outer, QSize size) {
		const auto right = std::max(size.width(), minLabelWidth) + skip;
		label->moveToRight(0, (outer.height() - size.height()) / 2);
		const auto width = std::max(
			sliderSt.seekSize.width(),
			outer.width() - right);
		slider->resizeToWidth(width);
		slider->moveToLeft(0, (outer.height() - slider->height()) / 2);
	}, label->lifetime());
	return {
		.widget = std::move(result),
		.slider = slider,
		.label = label,
	};
}

void AddLottieIconWithCircle(
		not_null<Ui::VerticalLayout*> layout,
		object_ptr<Ui::RpWidget> icon,
		QMargins iconPadding,
		QSize circleSize) {
	const auto iconRow = layout->add(
		std::move(icon),
		iconPadding,
		style::al_top);

	const auto circle = Ui::CreateChild<Ui::RpWidget>(
		iconRow->parentWidget());
	circle->lower();
	circle->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		const auto left = (circle->width() - circleSize.width()) / 2;
		const auto top = (circle->height() - circleSize.height()) / 2;
		p.setPen(Qt::NoPen);
		p.setBrush(st::activeButtonBg);
		p.drawEllipse(QRect(QPoint(left, top), circleSize));
	});

	iconRow->geometryValue() | rpl::on_next([=](const QRect &g) {
		circle->setGeometry(g);
	}, circle->lifetime());
}

void AddPremiumStar(
		not_null<Button*> button,
		bool credits,
		Fn<bool()> isPaused) {
	const auto stops = credits
		? Ui::Premium::CreditsIconGradientStops()
		: Ui::Premium::ButtonGradientStops();

	const auto ministarsContainer = Ui::CreateChild<Ui::RpWidget>(button);
	const auto &buttonSt = button->st();
	const auto fullHeight = buttonSt.height
		+ rect::m::sum::v(buttonSt.padding);
	using MiniStars = Ui::Premium::ColoredMiniStars;
	const auto ministars = button->lifetime().make_state<MiniStars>(
		ministarsContainer,
		false);
	ministars->setColorOverride(stops);

	const auto isPausedValue
		= button->lifetime().make_state<rpl::variable<bool>>(isPaused());
	isPausedValue->value() | rpl::on_next([=](bool value) {
		ministars->setPaused(value);
	}, ministarsContainer->lifetime());

	ministarsContainer->paintRequest(
	) | rpl::on_next([=] {
		(*isPausedValue) = isPaused();
		auto p = QPainter(ministarsContainer);
		{
			constexpr auto kScale = 0.35;
			const auto r = ministarsContainer->rect();
			p.translate(r.center());
			p.scale(kScale, kScale);
			p.translate(-r.center());
		}
		ministars->paint(p);
	}, ministarsContainer->lifetime());

	const auto badge = Ui::CreateChild<Ui::RpWidget>(button.get());

	auto star = [&] {
		const auto factor = style::DevicePixelRatio();
		const auto size = Size(st::settingsButtonNoIcon.style.font->ascent);
		auto image = QImage(
			size * factor,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(factor);
		image.fill(Qt::transparent);
		{
			auto p = QPainter(&image);
			auto star = QSvgRenderer(Ui::Premium::ColorizedSvg(stops));
			star.render(&p, Rect(size));
		}
		return image;
	}();
	badge->resize(star.size() / style::DevicePixelRatio());
	badge->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(badge);
		p.drawImage(0, 0, star);
	}, badge->lifetime());

	button->sizeValue(
	) | rpl::on_next([=](const QSize &s) {
		badge->moveToLeft(
			button->st().iconLeft
				+ (st::menuIconShop.width() - badge->width()) / 2,
			(s.height() - badge->height()) / 2);
		ministarsContainer->moveToLeft(
			badge->x() - (fullHeight - badge->height()) / 2,
			0);
	}, badge->lifetime());

	ministarsContainer->resize(fullHeight, fullHeight);
	ministars->setCenter(ministarsContainer->rect());
}

SectionSearchRow CreateSectionSearchRow(
		not_null<QWidget*> parent,
		const QString &query) {
	auto controller = std::make_unique<Ui::SearchFieldController>(query);
	auto rowView = controller->createRowView(
		parent,
		st::infoLayerMediaSearch);
	const auto row = rowView.wrap.release();
	const auto field = rowView.field.data();
	row->show();
	return {
		.controller = std::move(controller),
		.row = row,
		.field = field,
	};
}

QStringList SearchWords(const QString &text) {
	auto simple = text;
	return TextUtilities::PrepareSearchWords(simple.replace('#', ' '));
}

bool MatchesWords(const QStringList &terms, const QStringList &words) {
	for (const auto &word : words) {
		auto found = false;
		for (const auto &term : terms) {
			if (term.startsWith(word)) {
				found = true;
				break;
			}
		}
		if (!found) {
			return false;
		}
	}
	return true;
}

} // namespace Settings
