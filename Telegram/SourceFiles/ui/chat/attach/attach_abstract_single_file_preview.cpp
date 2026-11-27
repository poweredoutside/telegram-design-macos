/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_abstract_single_file_preview.h"

#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/text/text_options.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat_style.h"

namespace Ui {

AbstractSingleFilePreview::AbstractSingleFilePreview(
	QWidget *parent,
	const style::ComposeControls &st,
	AttachControls::Type type,
	const Text::MarkedContext &captionContext,
	bool macFileRow)
: AbstractSinglePreview(parent)
, _st(st)
, _type(type)
, _macFileRow(macFileRow)
, _captionContext(captionContext)
, _editMedia(this, _st.files.buttonFile)
, _deleteMedia(this, _st.files.buttonFile)
, _selectCheck(_st.files.selectCheck, [=] { update(); }) {
	const auto repaint = _captionContext.repaint;
	_captionContext.repaint = [=] {
		if (repaint) {
			repaint();
		}
		const auto rect = captionRect();
		if (rect.isEmpty()) {
			update();
		} else {
			update(rect);
		}
	};

	_editMedia->setIconOverride(&_st.files.buttonFileEdit);
	_deleteMedia->setIconOverride(&_st.files.buttonFileDelete);

	if (type == AttachControls::Type::Full) {
		_deleteMedia->show();
		_editMedia->show();
	} else if (type == AttachControls::Type::EditOnly) {
		_deleteMedia->hide();
		_editMedia->show();
	} else if (type == AttachControls::Type::None) {
		_deleteMedia->hide();
		_editMedia->hide();
	}
	setMouseTracking(true);
}

AbstractSingleFilePreview::~AbstractSingleFilePreview() = default;

rpl::producer<> AbstractSingleFilePreview::editRequests() const {
	return _editMedia->clicks() | rpl::to_empty;
}

rpl::producer<> AbstractSingleFilePreview::renameRequests() const {
	return _renameRequests.events();
}

rpl::producer<> AbstractSingleFilePreview::selectRequests() const {
	return _selectRequests.events();
}

rpl::producer<> AbstractSingleFilePreview::deleteRequests() const {
	return _deleteMedia->clicks() | rpl::to_empty;
}

rpl::producer<> AbstractSingleFilePreview::modifyRequests() const {
	return rpl::never<>();
}

void AbstractSingleFilePreview::setRenameEnabled(bool enabled) {
	if (_renameEnabled == enabled) {
		return;
	}
	_renameEnabled = enabled;
	if (!_renameEnabled) {
		_namePressed = false;
		applyCursor(style::cur_default);
	}
}

void AbstractSingleFilePreview::setSelectable(bool selectable) {
	_selectable = selectable && (_type == AttachControls::Type::Full);
}

void AbstractSingleFilePreview::setSelectionMode(bool enabled) {
	_selectionMode = enabled;
}

void AbstractSingleFilePreview::setSelected(
		bool selected,
		anim::type animated) {
	_selectCheck.setChecked(selected, animated);
}

bool AbstractSingleFilePreview::selected() const {
	return _selectCheck.checked();
}

void AbstractSingleFilePreview::setDisplayName(const QString &displayName) {
	_data.name = displayName;
	updateTextWidthFor(_data);
	updateDataGeometry();
	update();
}

void AbstractSingleFilePreview::setCaption(const TextWithTags &caption) {
	auto marked = TextWithEntities{
		caption.text,
		TextUtilities::ConvertTextTagsToEntities(caption.tags),
	};
	marked = TextUtilities::SingleLine(marked);
	_data.caption.setMarkedText(
		st::defaultTextStyle,
		marked,
		kMarkupTextOptions,
		_captionContext);
	updateTextWidthFor(_data);
	updateDataGeometry();
	update();
}

void AbstractSingleFilePreview::prepareThumbFor(
		Data &data,
		const QImage &preview) {
	if (preview.isNull()) {
		return;
	}

	auto originalWidth = preview.width();
	auto originalHeight = preview.height();
	const auto &st = st::attachPreviewThumbLayout;
	auto thumbWidth = st.thumbSize;
	if (originalWidth > originalHeight) {
		thumbWidth = (originalWidth * st.thumbSize) / originalHeight;
	}
	const auto options = Images::Option::RoundSmall;
	data.fileThumb = PixmapFromImage(Images::Prepare(
		preview,
		thumbWidth * style::DevicePixelRatio(),
		{ .options = options, .outer = { st.thumbSize, st.thumbSize } }));
}

void AbstractSingleFilePreview::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto margins = rowMargins();
	const auto &st = fileLayout(_data);
	const auto nameleft = st.thumbSize + st.thumbSkip;
	const auto nametop = st.nameTop;
	const auto statustop = st.statusTop;
	const auto x = margins.left();
	const auto y = margins.top();

	if (_macFileRow) {
		const auto inset = style::ConvertScale(12);
		const auto radius = style::ConvertScale(12);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect().adjusted(inset, 0, -inset, 0), radius, radius);
	}

	if (!isThumbedLayout(_data)) {
		QRect inner(
			style::rtlrect(x, y, st.thumbSize, st.thumbSize, width()));
		p.setPen(Qt::NoPen);

		if (_data.fileIsAudio && !_data.fileThumb.isNull()) {
			if (_macFileRow) {
				p.drawPixmap(inner, _data.fileThumb);
			} else {
				p.drawPixmap(inner.topLeft(), _data.fileThumb);
			}
		} else {
			PainterHighQualityEnabler hq(p);
			if (_macFileRow) {
				const auto radius = style::ConvertScale(8);
				p.setBrush(st::windowBg);
				p.drawRoundedRect(inner, radius, radius);
			} else {
				p.setBrush(_st.files.iconBg);
				p.drawEllipse(inner);
			}
		}
		const auto &icon = _data.fileIsAudio
			? (_data.fileThumb.isNull()
				? _st.files.iconPlay
				: st::historyFileThumbPlay)
			: _data.fileIsImage
			? _st.files.iconImage
			: _st.files.iconDocument;
		if (_macFileRow && !_data.fileIsAudio) {
			icon.paintInCenter(p, inner, st::windowActiveTextFg->c);
		} else {
			icon.paintInCenter(p, inner);
		}
	} else {
		QRect rthumb(
			style::rtlrect(x, y, st.thumbSize, st.thumbSize, width()));
		p.drawPixmap(rthumb.topLeft(), _data.fileThumb);
	}
	const auto checkSize = _st.files.selectCheck.size;
	const auto check = style::rtlrect(
		x + st.thumbSize - checkSize,
		y + st.thumbSize - checkSize,
		checkSize,
		checkSize,
		width());
	_selectCheck.paint(p, check.x(), check.y(), width());
	p.setFont(st::semiboldFont);
	p.setPen(_st.files.nameFg);
	p.drawTextLeft(
		x + nameleft,
		y + nametop, width(),
		_data.elidedName,
		_data.nameWidth);

	p.setFont(st::normalFont);
	p.setPen(_st.files.statusFg);
	p.drawTextLeft(
		x + nameleft,
		y + statustop,
		width(),
		_data.statusText,
		_data.statusWidth);
	if (!_data.caption.isEmpty()) {
		p.setPen(_st.files.nameFg);
		const auto captionTop = y
			+ st.thumbSize
			+ st::attachPreviewCaptionTopOffset;
		_data.caption.draw(p, {
			.position = {
				x,
				captionTop,
			},
			.outerWidth = width(),
			.availableWidth = _data.captionAvailableWidth,
			.align = style::al_left,
			.elisionLines = 1,
			.elisionBreakEverywhere = true,
		});
	}
}

void AbstractSingleFilePreview::resizeEvent(QResizeEvent *e) {
	const auto margins = rowMargins();
	const auto top = _macFileRow
		? margins.top()
			+ (fileLayout(_data).thumbSize - _editMedia->height()) / 2
		: st::sendBoxFileGroupSkipTop;
	const auto gap = _macFileRow ? 0 : st::sendBoxFileGroupEditInternalSkip;
	auto right = margins.right()
		+ (_macFileRow ? 0 : st::sendBoxFileGroupSkipRight);
	if (_type != AttachControls::Type::EditOnly) {
		_deleteMedia->moveToRight(right, top);
		right += gap + _deleteMedia->width();
	}
	_editMedia->moveToRight(right, top);
	if (_macFileRow && e->oldSize().width() != width()) {
		updateTextWidthFor(_data);
	}
}

bool AbstractSingleFilePreview::isThumbedLayout(const Data &data) const {
	return (!data.fileThumb.isNull() && !data.fileIsAudio);
}

void AbstractSingleFilePreview::updateTextWidthFor(Data &data) {
	const auto &st = fileLayout(data);
	const auto buttonsCount = (_type == AttachControls::Type::EditOnly)
		? 1
		: (_type == AttachControls::Type::Full)
		? 2
		: 0;
	const auto margins = rowMargins();
	const auto availableCaptionWidth = _macFileRow
		? (width() - margins.left() - margins.right()
			- _st.files.buttonFile.width * buttonsCount - style::ConvertScale(6))
		: (st::sendMediaPreviewSize
			- _st.files.buttonFile.width * buttonsCount
			- st::sendBoxAlbumGroupEditInternalSkip * buttonsCount
			- st::sendBoxAlbumGroupSkipRight);
	const auto availableFileWidth = std::max(
		0,
		availableCaptionWidth - st.thumbSize - st.thumbSkip);
	data.elidedName = st::semiboldFont->elided(
		data.name,
		availableFileWidth,
		Qt::ElideMiddle);
	data.nameWidth = st::semiboldFont->width(data.elidedName);
	if (_macFileRow) {
		setToolTip(data.elidedName == data.name
			? QString()
			: (u"<qt>"_q + data.name.toHtmlEscaped() + u"</qt>"_q));
	}
	data.statusWidth = st::normalFont->width(data.statusText);
	data.captionAvailableWidth = std::max(0, availableCaptionWidth);
}

void AbstractSingleFilePreview::updateDataGeometry() {
	const auto &st = fileLayout(_data);
	const auto margins = rowMargins();
	const auto height = margins.top() + st.thumbSize + margins.bottom()
		+ (_data.caption.isEmpty()
			? 0
			: (st::attachPreviewCaptionTopOffset + _data.caption.lineHeight()));
	resize(width(), height);
}

QMargins AbstractSingleFilePreview::rowMargins() const {
	const auto horizontal = _macFileRow
		? style::ConvertScale(22)
		: (st::boxPhotoPadding.left() + st::boxPhotoPadding.right()) / 2;
	const auto vertical = _macFileRow ? style::ConvertScale(8) : 0;
	return { horizontal, vertical, horizontal, vertical };
}

const style::HistoryFileLayout &AbstractSingleFilePreview::fileLayout(
		const Data &data) const {
	if (isThumbedLayout(data)) {
		return st::attachPreviewThumbLayout;
	} else if (!_macFileRow) {
		return st::attachPreviewLayout;
	}
	static const auto mac = [] {
		auto result = st::attachPreviewLayout;
		result.thumbSize = style::ConvertScale(36);
		result.thumbSkip = style::ConvertScale(10);
		const auto textHeight = st::semiboldFont->height + st::normalFont->height;
		result.nameTop = (result.thumbSize - textHeight) / 2;
		result.statusTop = result.nameTop + st::semiboldFont->height;
		return result;
	}();
	return mac;
}

QRect AbstractSingleFilePreview::captionRect() const {
	if (_data.caption.isEmpty()) {
		return {};
	}
	const auto margins = rowMargins();
	const auto &st = fileLayout(_data);
	const auto captionLineHeight = _data.caption.lineHeight();
	const auto top = margins.top() + st.thumbSize
		+ st::attachPreviewCaptionTopOffset;
	return QRect(
		margins.left(),
		top,
		_data.captionAvailableWidth,
		captionLineHeight) + st::attachPreviewCaptionRepaintMargin;
}

void AbstractSingleFilePreview::setData(Data data) {
	_data = std::move(data);
	updateTextWidthFor(_data);
	updateDataGeometry();
}

void AbstractSingleFilePreview::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	} else if (selectingByClick(e->modifiers())) {
		_selectPressed = true;
	} else if (isOverName(e->pos())) {
		_namePressed = true;
	}
}

void AbstractSingleFilePreview::mouseMoveEvent(QMouseEvent *e) {
	applyCursor(isOverName(e->pos())
		? style::cur_pointer
		: style::cur_default);
}

void AbstractSingleFilePreview::mouseReleaseEvent(QMouseEvent *e) {
	const auto selectPressed = base::take(_selectPressed);
	const auto namePressed = base::take(_namePressed);
	if (e->button() != Qt::LeftButton) {
		return;
	} else if (selectPressed && rect().contains(e->pos())) {
		_selectRequests.fire({});
	} else if (namePressed && isOverName(e->pos())) {
		_renameRequests.fire({});
	}
}

QRect AbstractSingleFilePreview::nameRect() const {
	const auto margins = rowMargins();
	const auto &st = fileLayout(_data);
	return style::rtlrect(
		margins.left() + st.thumbSize + st.thumbSkip,
		margins.top() + st.nameTop,
		_data.nameWidth,
		st::semiboldFont->height,
		width());
}

bool AbstractSingleFilePreview::isOverName(QPoint point) const {
	return _renameEnabled && !_selectionMode && nameRect().contains(point);
}

bool AbstractSingleFilePreview::selectingByClick(
		Qt::KeyboardModifiers modifiers) const {
	return _selectable
		&& (_selectionMode || modifiers.testFlag(Qt::ControlModifier));
}

void AbstractSingleFilePreview::applyCursor(style::cursor cursor) {
	if (_cursor != cursor) {
		_cursor = cursor;
		setCursor(_cursor);
	}
}

} // namespace Ui
