/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_video_userpic.h"

#include "base/unixtime.h"
#include "core/file_location.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/ui/dialogs_layout.h"
#include "ui/painter.h"
#include "ui/userpic_rounding.h"
#include "styles/style_dialogs.h"

namespace Dialogs::Ui {

VideoUserpic::VideoUserpic(not_null<PeerData*> peer, Fn<void()> repaint)
: _peer(peer)
, _repaint(std::move(repaint)) {
}

VideoUserpic::~VideoUserpic() = default;

int VideoUserpic::frameIndex() const {
	return -1;
}

void VideoUserpic::paintLeft(
		Painter &p,
		Ui::PeerUserpicView &view,
		int x,
		int y,
		int w,
		int size,
		bool paused) {
	_lastSize = size;

	const auto photoId = _peer->userpicPhotoId();
	if (_videoPhotoId != photoId) {
		_videoPhotoId = photoId;
		_video = nullptr;
		_videoPhotoMedia = nullptr;
		_roundedFrame = QImage();
		_roundedFrameSource = 0;
		const auto photo = _peer->owner().photo(photoId);
		if (photo->isNull()) {
			_peer->updateFullForced();
		} else {
			_videoPhotoMedia = photo->createMediaView();
			_videoPhotoMedia->videoWanted(
				Data::PhotoSize::Small,
				_peer->userpicPhotoOrigin());
		}
	}
	if (!_video && !_video.isBad()) {
		if (!_videoPhotoMedia) {
			const auto photo = _peer->owner().photo(photoId);
			if (!photo->isNull()) {
				_videoPhotoMedia = photo->createMediaView();
				_videoPhotoMedia->videoWanted(
					Data::PhotoSize::Small,
					_peer->userpicPhotoOrigin());
			}
		}
		if (_videoPhotoMedia) {
			auto small = _videoPhotoMedia->videoContent(
				Data::PhotoSize::Small);
			auto bytes = small.isEmpty()
				? _videoPhotoMedia->videoContent(Data::PhotoSize::Large)
				: small;
			if (!bytes.isEmpty()) {
				auto callback = [=](Media::Clip::Notification notification) {
					clipCallback(notification);
				};
				_video = Media::Clip::MakeReader(
					Core::FileLocation(),
					std::move(bytes),
					std::move(callback));
			}
		}
	}
	if (_video && _video->ready()) {
		startReady();

		const auto now = paused ? crl::time(0) : crl::now();
		const auto options = request(size);
		const auto frame = _video->current(options, now);
		const auto rounding = Ui::DisplayUserpicRounding(_peer);
		const auto needsMask = (options.radius == ImageRoundRadius::None)
			&& (rounding.radius > 0.);
		if (needsMask && _roundedFrameSource != frame.cacheKey()) {
			_roundedFrameSource = frame.cacheKey();
			_roundedFrame = Ui::ApplyUserpicShape(frame, rounding);
		}
		p.drawImage(rtl() ? w - x - size : x, y, needsMask ? _roundedFrame : frame);
	} else {
		_peer->paintUserpicLeft(p, view, x, y, w, size);
	}
}

Media::Clip::FrameRequest VideoUserpic::request(int size) const {
	const auto rounding = Ui::DisplayUserpicRounding(_peer);
	return {
		.frame = { size, size },
		.outer = { size, size },
		.factor = style::DevicePixelRatio(),
		.radius = (rounding.radius >= .5)
			? ImageRoundRadius::Ellipse
			: ImageRoundRadius::None,
	};
}

bool VideoUserpic::startReady(int size) {
	if (!_video->ready() || _video->started()) {
		return false;
	} else if (!_lastSize) {
		_lastSize = size ? size : _video->width();
	}
	_video->start(request(_lastSize));
	_repaint();
	return true;
}

void VideoUserpic::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;

	switch (notification) {
	case Notification::Reinit: {
		if (_video->state() == State::Error) {
			_video.setBad();
		} else if (!startReady()) {
			_repaint();
		}
	} break;

	case Notification::Repaint: _repaint(); break;
	}
}

void PaintUserpic(
		Painter &p,
		not_null<Entry*> entry,
		PeerData *peer,
		VideoUserpic *videoUserpic,
		PeerUserpicView &view,
		const Ui::PaintContext &context) {
	if (peer) {
		PaintUserpic(
			p,
			peer,
			videoUserpic,
			view,
			context.st->padding.left(),
			context.st->padding.top(),
			context.width,
			context.st->photoSize,
			context.paused);
	} else {
		entry->paintUserpic(p, view, context);
	}
}

void PaintUserpic(
		Painter &p,
		not_null<PeerData*> peer,
		Ui::VideoUserpic *videoUserpic,
		Ui::PeerUserpicView &view,
		int x,
		int y,
		int outerWidth,
		int size,
		bool paused) {
	if (videoUserpic) {
		videoUserpic->paintLeft(p, view, x, y, outerWidth, size, paused);
	} else {
		peer->paintUserpicLeft(p, view, x, y, outerWidth, size);
	}
}

void PaintSenderOnlineBadge(
		Painter &painter,
		not_null<PeerData*> peer,
		int left,
		int top,
		int outerWidth,
		int size) {
	const auto user = peer->asUser();
	const auto now = base::unixtime::now();
	if (!user || user->isSelf() || !Data::IsUserOnline(user, now)) {
		return;
	}
	user->owner().watchForOffline(user, now);
	const auto diameter = size / 3.5;
	const auto badgeLeft = rtl()
		? (outerWidth - left - size)
		: (left + size - diameter);
	painter.save();
	{
		auto hq = PainterHighQualityEnabler(painter);
		painter.setPen(QPen(st::windowBg, 2. * st::lineWidth));
		painter.setBrush(st::dialogsOnlineBadgeFg);
		painter.drawEllipse(QRectF(
			badgeLeft, top + size - diameter, diameter, diameter));
	}
	painter.restore();
}

} // namespace Dialogs::Ui
