#pragma once

#include "base/weak_ptr.h"

#include <QtGui/QImage>
#include <rpl/event_stream.h>
#include <rpl/variable.h>

namespace Media::Streaming {
class Instance;
struct Update;
} // namespace Media::Streaming

namespace Window::Theme {

class LiveWallpaperVideo final : public base::has_weak_ptr {
public:
	enum class Status { Loading, Ready, Invalid, TooLong };

	explicit LiveWallpaperVideo(const QString &path);
	~LiveWallpaperVideo();

	[[nodiscard]] Status status() const;
	[[nodiscard]] rpl::producer<Status> statusValue() const;
	[[nodiscard]] rpl::producer<> frameUpdates() const;
	[[nodiscard]] const QImage &frame() const;
	void setPaused(bool paused);

private:
	void handleUpdate(Media::Streaming::Update &&update);
	void refreshFrame();
	void fail(Status status);

	std::unique_ptr<Media::Streaming::Instance> _instance;
	rpl::variable<Status> _status = Status::Loading;
	rpl::event_stream<> _frameUpdates;
	QSize _frameSize;
	QImage _frame;
	crl::time _lastFrame = 0;
	bool _paused = false;

};

[[nodiscard]] QString SavedLiveWallpaperPath();
void ApplyLiveWallpaper(const QString &path, Fn<void(bool)> done);
void UseStaticWallpaper();
void ObserveLiveWallpaper(not_null<QWidget*> widget);
void PaintWallpaperFrame(QPainter &p, const QImage &frame, QSize fill, QRect clip);
[[nodiscard]] bool PaintLiveWallpaper(QPainter &p, QSize fill, QRect clip);
void FinishLiveWallpaper();

} // namespace Window::Theme
