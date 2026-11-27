#include "window/themes/window_theme_live_wallpaper.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_telega_preferences.h"
#include "media/streaming/media_streaming_document.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_loader_local.h"
#include "media/streaming/media_streaming_player.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QUuid>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>

namespace Window::Theme {
namespace {

constexpr auto kMaxDuration = crl::time(60'000);
constexpr auto kFrameInterval = crl::time(32);
constexpr auto kWallpaperId = "telega.wallpaper.videoId";
constexpr auto kWallpaperEnabled = "telega.wallpaper.videoEnabled";

[[nodiscard]] QString WallpaperFolder() {
	return cWorkingDir() + u"tdata/telega-wallpapers"_q;
}

[[nodiscard]] QString WallpaperPath(const QString &id) {
	if (id.isEmpty() || QUuid(id).toString(QUuid::WithoutBraces) != id) {
		return QString();
	}
	return QDir(WallpaperFolder()).filePath(id + u".video"_q);
}

[[nodiscard]] QString SavedWallpaperId() {
	return QString::fromLatin1(
		Core::App().settings().readPref<QByteArray>(
			Core::Telega::StyleThemePreference(kWallpaperId)));
}

[[nodiscard]] bool LiveWallpaperEnabled() {
	return Core::App().settings().readPref<bool>(
		Core::Telega::StyleThemePreference(kWallpaperEnabled), false);
}

[[nodiscard]] bool CanRepaint(not_null<QWidget*> widget) {
	return QGuiApplication::applicationState() == Qt::ApplicationActive
		&& widget->isVisible()
		&& !widget->window()->isMinimized();
}

class LiveWallpaper final : public base::has_weak_ptr {
public:
	LiveWallpaper();
	~LiveWallpaper();

	void open(const QString &path);
	void observe(not_null<QWidget*> widget);
	void apply(const QString &path, Fn<void(bool)> done);
	void useStatic();
	[[nodiscard]] QImage frame() const;

private:
	void repaintTargets();
	void enable(const QString &id);

	std::map<QWidget*, QMetaObject::Connection> _targets;
	QMetaObject::Connection _activation;
	std::unique_ptr<LiveWallpaperVideo> _video;
	rpl::lifetime _videoLifetime;
	QString _pendingId;

};

std::unique_ptr<LiveWallpaper> GlobalWallpaper;

[[nodiscard]] LiveWallpaper &Wallpaper() {
	if (!GlobalWallpaper) {
		GlobalWallpaper = std::make_unique<LiveWallpaper>();
	}
	return *GlobalWallpaper;
}

LiveWallpaper::LiveWallpaper() {
	_activation = QObject::connect(
		qApp,
		&QGuiApplication::applicationStateChanged,
		qApp,
		[=] { repaintTargets(); });
	if (LiveWallpaperEnabled()) {
		open(SavedLiveWallpaperPath());
	}
}

LiveWallpaper::~LiveWallpaper() {
	QObject::disconnect(_activation);
	for (const auto &[widget, connection] : _targets) {
		QObject::disconnect(connection);
	}
}

void LiveWallpaper::open(const QString &path) {
	_videoLifetime.destroy();
	_video = path.isEmpty() ? nullptr : std::make_unique<LiveWallpaperVideo>(path);
	if (_video) {
		_video->frameUpdates() | rpl::on_next([=] {
			repaintTargets();
		}, _videoLifetime);
	}
	repaintTargets();
}

void LiveWallpaper::observe(not_null<QWidget*> widget) {
	if (!_targets.contains(widget.get())) {
		_targets.emplace(widget.get(), QObject::connect(
			widget.get(),
			&QObject::destroyed,
			qApp,
			[=] { _targets.erase(widget.get()); }));
	}
	if (_video && CanRepaint(widget)) {
		_video->setPaused(false);
	}
}

void LiveWallpaper::repaintTargets() {
	auto playing = false;
	for (const auto &[widget, connection] : _targets) {
		if (CanRepaint(widget)) {
			widget->update();
			playing = true;
		}
	}
	if (_video) {
		_video->setPaused(!playing);
	}
}

void LiveWallpaper::enable(const QString &id) {
	auto &settings = Core::App().settings();
	const auto previous = SavedLiveWallpaperPath();
	const auto next = WallpaperPath(id);
	settings.writePref<QByteArray>(
		Core::Telega::StyleThemePreference(kWallpaperId), id.toLatin1());
	settings.writePref<bool>(Core::Telega::StyleThemePreference(kWallpaperEnabled), true);
	Core::App().saveSettingsDelayed();
	open(next);
	if (!previous.isEmpty() && previous != next) {
		QFile::remove(previous);
	}
}

void LiveWallpaper::apply(const QString &path, Fn<void(bool)> done) {
	if (!_pendingId.isEmpty() || path.isEmpty()) {
		done(false);
		return;
	}
	if (path == SavedLiveWallpaperPath()) {
		enable(SavedWallpaperId());
		done(true);
		return;
	}
	const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	_pendingId = id;
	const auto folder = WallpaperFolder();
	const auto target = WallpaperPath(id);
	crl::async([=, weak = base::make_weak(this), done = std::move(done)]() mutable {
		const auto copied = QDir().mkpath(folder) && QFile::copy(path, target);
		crl::on_main([=, done = std::move(done)]() mutable {
			if (const auto strong = weak.get(); strong && strong->_pendingId == id) {
				strong->_pendingId.clear();
				if (copied) {
					strong->enable(id);
				}
				done(copied);
			} else {
				if (copied) {
					QFile::remove(target);
				}
				if (strong) {
					done(false);
				}
			}
		});
	});
}

void LiveWallpaper::useStatic() {
	if (!_video && _pendingId.isEmpty() && !LiveWallpaperEnabled()) {
		return;
	}
	_pendingId.clear();
	Core::App().settings().writePref<bool>(
		Core::Telega::StyleThemePreference(kWallpaperEnabled), false);
	Core::App().saveSettingsDelayed();
	open({});
}

QImage LiveWallpaper::frame() const {
	return _video ? _video->frame() : QImage();
}

} // namespace

LiveWallpaperVideo::LiveWallpaperVideo(const QString &path) {
	using namespace Media::Streaming;
	auto loader = MakeFileLoader(path);
	if (!loader || loader->size() <= 0) {
		_status = Status::Invalid;
		return;
	}
	_instance = std::make_unique<Instance>(
		std::make_shared<Document>(std::move(loader)),
		nullptr);
	_instance->lockPlayer();
	_instance->player().updates() | rpl::on_next_error([=](Update &&update) {
		handleUpdate(std::move(update));
	}, [=](Error &&) {
		fail(Status::Invalid);
	}, _instance->lifetime());
	auto options = PlaybackOptions();
	options.mode = Mode::Video;
	options.loop = true;
	options.hwAllowed = true;
	_instance->play(options);
}

LiveWallpaperVideo::~LiveWallpaperVideo() = default;

LiveWallpaperVideo::Status LiveWallpaperVideo::status() const {
	return _status.current();
}

rpl::producer<LiveWallpaperVideo::Status> LiveWallpaperVideo::statusValue() const {
	return _status.value();
}

rpl::producer<> LiveWallpaperVideo::frameUpdates() const {
	return _frameUpdates.events();
}

const QImage &LiveWallpaperVideo::frame() const {
	return _frame;
}

void LiveWallpaperVideo::setPaused(bool paused) {
	if (!_instance || !_instance->active()
		|| std::exchange(_paused, paused) == paused) {
		return;
	}
	if (paused) {
		_instance->pause();
	} else {
		_instance->resume();
	}
}

void LiveWallpaperVideo::handleUpdate(Media::Streaming::Update &&update) {
	using namespace Media::Streaming;
	if (const auto info = std::get_if<Information>(&update.data)) {
		const auto duration = info->video.state.duration;
		if (duration <= 0 || duration == Media::kDurationUnavailable
			|| info->video.size.isEmpty()) {
			fail(Status::Invalid);
			return;
		} else if (duration > kMaxDuration) {
			fail(Status::TooLong);
			return;
		}
		const auto source = info->video.size;
		const auto limit = QSize(1280, 1280);
		_frameSize = (source.width() > limit.width()
			|| source.height() > limit.height())
			? source.scaled(limit, Qt::KeepAspectRatio)
			: source;
		refreshFrame();
		_status = Status::Ready;
	} else if (std::holds_alternative<UpdateVideo>(update.data)
		&& status() == Status::Ready) {
		if (crl::now() - _lastFrame >= kFrameInterval) {
			refreshFrame();
		}
	}
}

void LiveWallpaperVideo::refreshFrame() {
	auto request = Media::Streaming::FrameRequest();
	request.resize = request.outer = _frameSize;
	if (auto frame = _instance->frame(request); !frame.isNull()) {
		_frame = std::move(frame);
		_lastFrame = crl::now();
		_frameUpdates.fire({});
	}
}

void LiveWallpaperVideo::fail(Status status) {
	crl::on_main(base::make_weak(this), [=] {
		_instance.reset();
		_frame = QImage();
		_status = status;
		_frameUpdates.fire({});
	});
}

QString SavedLiveWallpaperPath() {
	const auto path = WallpaperPath(SavedWallpaperId());
	return QFile::exists(path) ? path : QString();
}

void ApplyLiveWallpaper(const QString &path, Fn<void(bool)> done) {
	Wallpaper().apply(path, std::move(done));
}

void UseStaticWallpaper() {
	Wallpaper().useStatic();
}

void ObserveLiveWallpaper(not_null<QWidget*> widget) {
	Wallpaper().observe(widget);
}

void PaintWallpaperFrame(QPainter &p, const QImage &frame, QSize fill, QRect clip) {
	if (frame.isNull() || fill.isEmpty() || clip.isEmpty()) {
		return;
	}
	const auto scaled = frame.size().scaled(fill, Qt::KeepAspectRatioByExpanding);
	const auto target = QRect(
		{ (fill.width() - scaled.width()) / 2, (fill.height() - scaled.height()) / 2 },
		scaled);
	p.save();
	p.setClipRect(clip, Qt::IntersectClip);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(target, frame);
	p.restore();
}

bool PaintLiveWallpaper(QPainter &p, QSize fill, QRect clip) {
	const auto frame = GlobalWallpaper ? GlobalWallpaper->frame() : QImage();
	if (frame.isNull()) {
		return false;
	}
	PaintWallpaperFrame(p, frame, fill, clip);
	return true;
}

void FinishLiveWallpaper() {
	GlobalWallpaper.reset();
}

} // namespace Window::Theme
