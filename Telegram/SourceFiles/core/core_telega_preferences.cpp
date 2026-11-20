#include "core/core_telega_preferences.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "history/history.h"
#include "history/history_item.h"
#include "ui/userpic_rounding.h"

#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtCore/QTime>

namespace Core::Telega {
namespace {

constexpr auto kGoogleTranslation = "telega.translation.use-google";
constexpr auto kHideStories = "telega.dialogs.hide-stories";
constexpr auto kProtectWindow = "telega.window.capture-protection";
constexpr auto kCommaAfterMention = "telega.compose.comma-after-mention";
constexpr auto kHideCopyToast = "telega.messages.hide-copy-toast";
constexpr auto kHideBubbleTails = "telega.messages.hide-bubble-tails";
constexpr auto kHideFastShare = "telega.messages.hide-fast-share";
constexpr auto kPlainReplyPreviews = "telega.messages.plain-reply-previews";
constexpr auto kPauseVideoInBackground = "telega.media.pause-in-background";
constexpr auto kPreferOriginalVideo = "telega.media.prefer-original-video";
constexpr auto kVideoSeekStep = "telega.media.video-seek-step";
constexpr auto kDefaultPhotoHd = "telega.media.default-photo-hd";
constexpr auto kGifSearchBot = "telega.media.gif-search-bot";
constexpr auto kBottomAction = "telega.messages.channel-bottom-action";
constexpr auto kStickerTime = "telega.messages.sticker-time";
constexpr auto kStickerScale = "telega.messages.sticker-scale";
constexpr auto kNavigation = "telega.appearance.navigation";
constexpr auto kDownloadMode = "telega.transfer.download-mode";
constexpr auto kAppIcon = "telega.appearance.app-icon";
constexpr auto kAppIconSquare = "telega.appearance.app-icon-square";

AppIconAppearance &AppIconAtStartup() {
	static auto appearance = AppIconAppearance();
	return appearance;
}

rpl::variable<ChannelBottomAction> &ChannelAction() {
	static auto value = rpl::variable<ChannelBottomAction>(ChannelBottomAction(
		std::clamp(App().settings().readPref<int>(kBottomAction, 0), 0, 2)));
	return value;
}

StickerTimestamp &StickerTimeSetting() {
	static auto value = StickerTimestamp(std::clamp(
		App().settings().readPref<int>(kStickerTime, 0), 0, 2));
	return value;
}

int &StickerScaleSetting() {
	static auto value = std::clamp(
		App().settings().readPref<int>(kStickerScale, 100), 60, 140);
	return value;
}

template <DisplayOption Option>
auto DisplaySetting(const char *key) {
	static auto value = rpl::variable<bool>(
		App().settings().readPref<bool>(key, false));
	return std::pair(key, &value);
}

std::pair<const char*, rpl::variable<bool>*> DisplaySetting(
		DisplayOption option) {
	switch (option) {
	case DisplayOption::ProfilePhotoDc:
		return DisplaySetting<DisplayOption::ProfilePhotoDc>(
			"telega.profile.show-photo-dc");
	case DisplayOption::RelativeLastSeen:
		return DisplaySetting<DisplayOption::RelativeLastSeen>(
			"telega.profile.relative-last-seen");
	case DisplayOption::WideChannelPosts:
		return DisplaySetting<DisplayOption::WideChannelPosts>(
			"telega.messages.wide-channel-posts");
	case DisplayOption::TimeWithSeconds:
		return DisplaySetting<DisplayOption::TimeWithSeconds>(
			"telega.messages.time-with-seconds");
	case DisplayOption::HideDefaultGreetingSticker:
		return DisplaySetting<DisplayOption::HideDefaultGreetingSticker>(
			"telega.compose.hide-default-greeting-sticker");
	case DisplayOption::EditedIcon:
		return DisplaySetting<DisplayOption::EditedIcon>(
			"telega.messages.edited-icon");
	case DisplayOption::ForwardCount:
		return DisplaySetting<DisplayOption::ForwardCount>(
			"telega.messages.forward-count");
	case DisplayOption::ExactViewCounts:
		return DisplaySetting<DisplayOption::ExactViewCounts>(
			"telega.messages.exact-view-counts");
	case DisplayOption::RoundedContactAvatars:
		return DisplaySetting<DisplayOption::RoundedContactAvatars>(
			"telega.appearance.rounded-contact-avatars");
	case DisplayOption::HideSidebarStatus:
		return DisplaySetting<DisplayOption::HideSidebarStatus>(
			"telega.appearance.hide-sidebar-status");
	case DisplayOption::HideSenderMiniAvatars:
		return DisplaySetting<DisplayOption::HideSenderMiniAvatars>(
			"telega.dialogs.hide-sender-mini-avatars");
	case DisplayOption::AccentSenderNames:
		return DisplaySetting<DisplayOption::AccentSenderNames>(
			"telega.messages.accent-sender-names");
	case DisplayOption::HidePrivateReactions:
		return DisplaySetting<DisplayOption::HidePrivateReactions>(
			"telega.messages.hide-private-reactions");
	case DisplayOption::HideGroupReactions:
		return DisplaySetting<DisplayOption::HideGroupReactions>(
			"telega.messages.hide-group-reactions");
	case DisplayOption::HideChannelReactions:
		return DisplaySetting<DisplayOption::HideChannelReactions>(
			"telega.messages.hide-channel-reactions");
	case DisplayOption::HideSendAs:
		return DisplaySetting<DisplayOption::HideSendAs>(
			"telega.compose.hide-send-as");
	case DisplayOption::QuoteReplies:
		return DisplaySetting<DisplayOption::QuoteReplies>(
			"telega.compose.quote-replies");
	case DisplayOption::CountCharacters:
		return DisplaySetting<DisplayOption::CountCharacters>(
			"telega.compose.count-characters");
	case DisplayOption::SelectServiceMessages:
		return DisplaySetting<DisplayOption::SelectServiceMessages>(
			"telega.messages.select-service-messages");
	case DisplayOption::HideAllChatsFolder:
		return DisplaySetting<DisplayOption::HideAllChatsFolder>(
			"telega.dialogs.hide-all-chats-folder");
	case DisplayOption::PauseVoiceInBackground:
		return DisplaySetting<DisplayOption::PauseVoiceInBackground>(
			"telega.media.pause-voice-in-background");
	case DisplayOption::PauseRoundVideoInBackground:
		return DisplaySetting<DisplayOption::PauseRoundVideoInBackground>(
			"telega.media.pause-round-video-in-background");
	case DisplayOption::ProfileMusicCover:
		return DisplaySetting<DisplayOption::ProfileMusicCover>(
			"telega.profile.music-cover");
	case DisplayOption::UploadBoost:
		return DisplaySetting<DisplayOption::UploadBoost>(
			"telega.transfer.upload-boost");
	case DisplayOption::IgnorePeerThemes:
		return DisplaySetting<DisplayOption::IgnorePeerThemes>(
			"telega.appearance.ignore-peer-themes");
	case DisplayOption::HideTypingIndicator:
		return DisplaySetting<DisplayOption::HideTypingIndicator>(
			"telega.privacy.hide-typing-indicator");
	case DisplayOption::OnlineSenderBadge:
		return DisplaySetting<DisplayOption::OnlineSenderBadge>(
			"telega.messages.online-sender-badge");
	case DisplayOption::HideCopyPhotoAction:
		return DisplaySetting<DisplayOption::HideCopyPhotoAction>(
			"telega.messages.hide-copy-photo-action");
	case DisplayOption::HideSaveMediaAction:
		return DisplaySetting<DisplayOption::HideSaveMediaAction>(
			"telega.messages.hide-save-media-action");
	case DisplayOption::RepeatMessageAction:
		return DisplaySetting<DisplayOption::RepeatMessageAction>(
			"telega.messages.repeat-action");
	case DisplayOption::MessageDetailsAction:
		return DisplaySetting<DisplayOption::MessageDetailsAction>(
			"telega.messages.details-action");
	case DisplayOption::PauseAutomaticChatReads:
		return DisplaySetting<DisplayOption::PauseAutomaticChatReads>(
			"telega.privacy.pause-automatic-chat-reads");
	case DisplayOption::HideStoryViewReceipts:
		return DisplaySetting<DisplayOption::HideStoryViewReceipts>(
			"telega.privacy.hide-story-view-receipts");
	case DisplayOption::HideOnlineAnnouncements:
		return DisplaySetting<DisplayOption::HideOnlineAnnouncements>(
			"telega.privacy.hide-online-announcements");
	case DisplayOption::InlineCalculator:
		return DisplaySetting<DisplayOption::InlineCalculator>(
			"telega.compose.inline-calculator");
	case DisplayOption::QuickMessageActions:
		return DisplaySetting<DisplayOption::QuickMessageActions>(
			"telega.messages.quick-actions");
	case DisplayOption::PreviewPollResults:
		return DisplaySetting<DisplayOption::PreviewPollResults>(
			"telega.messages.preview-poll-results");
	case DisplayOption::SystemEmoji:
		return DisplaySetting<DisplayOption::SystemEmoji>(
			"telega.appearance.system-emoji");
	case DisplayOption::KeepUnreadWhenSending:
		return DisplaySetting<DisplayOption::KeepUnreadWhenSending>(
			"telega.privacy.keep-unread-when-sending");
	case DisplayOption::FilterVisualNoise:
		return DisplaySetting<DisplayOption::FilterVisualNoise>(
			"telega.appearance.filter-visual-noise");
	case DisplayOption::UniformAvatarRadius:
		return DisplaySetting<DisplayOption::UniformAvatarRadius>(
			"telega.appearance.uniform-avatar-radius");
	case DisplayOption::VisibleGroupAuthors:
		return DisplaySetting<DisplayOption::VisibleGroupAuthors>(
			"telega.privacy.visible-group-authors");
	case DisplayOption::SetOfflineAfterSending:
		return DisplaySetting<DisplayOption::SetOfflineAfterSending>(
			"telega.privacy.offline-after-sending");
	case DisplayOption::HideSponsoredMessages:
		return DisplaySetting<DisplayOption::HideSponsoredMessages>(
			"telega.appearance.hide-sponsored-messages");
	}
	Unexpected("Telega display option.");
}

bool ValidVideoSeekStep(int seconds) {
	return (seconds == 5 || seconds == 10 || seconds == 15 || seconds == 30);
}

rpl::variable<bool> &GoogleTranslationEnabled() {
	static auto result = rpl::variable<bool>(
		App().settings().readPref<bool>(kGoogleTranslation, false));
	return result;
}

rpl::variable<bool> &StoriesHidden() {
	static auto result = rpl::variable<bool>(
		App().settings().readPref<bool>(kHideStories, false));
	return result;
}

rpl::variable<bool> &WindowProtected() {
	static auto result = rpl::variable<bool>(
		App().settings().readPref<bool>(kProtectWindow, false));
	return result;
}

rpl::variable<bool> &MentionCommaEnabled() {
	static auto result = rpl::variable<bool>(
		App().settings().readPref<bool>(kCommaAfterMention, false));
	return result;
}

rpl::variable<bool> &CopyToastHidden() {
	static auto result = rpl::variable<bool>(
		App().settings().readPref<bool>(kHideCopyToast, false));
	return result;
}

rpl::variable<bool> &BubbleTailsHidden() {
	static auto result = rpl::variable<bool>(
		App().settings().readPref<bool>(kHideBubbleTails, false));
	return result;
}

rpl::variable<bool> &FastShareHidden() {
	static auto result = rpl::variable<bool>(
		App().settings().readPref<bool>(kHideFastShare, false));
	return result;
}

rpl::variable<bool> &ReplyPreviewsPlain() {
	static auto result = rpl::variable<bool>(
		App().settings().readPref<bool>(kPlainReplyPreviews, false));
	return result;
}

rpl::variable<bool> &BackgroundVideoPaused() {
	static auto result = rpl::variable<bool>(
		App().settings().readPref<bool>(kPauseVideoInBackground, false));
	return result;
}

rpl::variable<bool> &OriginalVideoPreferred() {
	static auto result = rpl::variable<bool>(
		App().settings().readPref<bool>(kPreferOriginalVideo, false));
	return result;
}

rpl::variable<bool> &PhotoHdDefault() {
	static auto result = rpl::variable<bool>(
		App().settings().readPref<bool>(kDefaultPhotoHd, false));
	return result;
}

void Set(bool enabled, const char *key, rpl::variable<bool> &variable) {
	if (variable.current() != enabled) {
		App().settings().writePref<bool>(key, enabled);
		App().saveSettingsDelayed();
		variable = enabled;
	}
}

} // namespace

bool DisplayEnabled(DisplayOption option) {
	return DisplaySetting(option).second->current();
}

rpl::producer<bool> DisplayValue(DisplayOption option) {
	return DisplaySetting(option).second->value();
}

void SetDisplayEnabled(DisplayOption option, bool enabled) {
	const auto [key, value] = DisplaySetting(option);
	Set(enabled, key, *value);
}

QString GifSearchBot() {
	return QString::fromUtf8(App().settings().readPref<QByteArray>(kGifSearchBot));
}

bool SetGifSearchBot(QString username) {
	static const auto pattern = QRegularExpression(u"\\A[A-Za-z][A-Za-z0-9_]{0,31}\\z"_q);
	username = username.trimmed();
	if (username.startsWith(u'@')) {
		username.remove(0, 1);
	}
	if (!username.isEmpty() && !pattern.match(username).hasMatch()) {
		return false;
	}
	App().settings().writePref<QByteArray>(kGifSearchBot, username.toLower().toUtf8());
	App().saveSettingsDelayed();
	return true;
}

ChannelBottomAction BottomAction() {
	return ChannelAction().current();
}

rpl::producer<ChannelBottomAction> BottomActionValue() {
	return ChannelAction().value();
}

void SetBottomAction(ChannelBottomAction action) {
	App().settings().writePref<int>(kBottomAction, int(action));
	App().saveSettingsDelayed();
	ChannelAction() = action;
}

StickerTimestamp StickerTime() {
	return StickerTimeSetting();
}

void SetStickerTime(StickerTimestamp mode) {
	StickerTimeSetting() = mode;
	App().settings().writePref<int>(kStickerTime, int(mode));
	App().saveSettingsDelayed();
}

int StickerScale() {
	return StickerScaleSetting();
}

void SetStickerScale(int percent) {
	StickerScaleSetting() = std::clamp(percent, 60, 140);
	App().settings().writePref<int>(kStickerScale, StickerScaleSetting());
	App().saveSettingsDelayed();
}

const QRegularExpression &TelegaVisualNoise() {
	static const auto pattern = QRegularExpression(
		u"[\\x{0300}-\\x{036F}]{4,}|[\\x{202A}-\\x{202E}]"_q);
	return pattern;
}

QString FilterDisplayName(const QString &name) {
	if (!DisplayEnabled(DisplayOption::FilterVisualNoise)
		|| !TelegaVisualNoise().match(name).hasMatch()) {
		return name;
	}
	auto result = name;
	return result.remove(TelegaVisualNoise());
}

bool SkipSendReadReceipt() {
	return DisplayEnabled(DisplayOption::PauseAutomaticChatReads)
		&& DisplayEnabled(DisplayOption::KeepUnreadWhenSending);
}

QStringList DefaultNavigation() {
	return { u"profile"_q, u"divider"_q, u"group"_q, u"channel"_q,
		u"contacts"_q, u"calls"_q, u"saved"_q, u"wallet"_q };
}

QStringList NavigationItems() {
	return DefaultNavigation() + QStringList{ u"ai"_q, u"feed"_q };
}

QStringList Navigation() {
	const auto bytes = App().settings().readPref<QByteArray>(kNavigation);
	if (bytes.isEmpty()) {
		return DefaultNavigation();
	}
	const auto available = NavigationItems();
	const auto legacy = !bytes.startsWith("v2:");
	auto result = QStringList();
	if (legacy) {
		result = { u"profile"_q, u"divider"_q };
	}
	for (const auto &item : QString::fromUtf8(
			legacy ? bytes : bytes.mid(3)).split(u',')) {
		if (available.contains(item) && !result.contains(item)) {
			result.push_back(item);
		}
	}
	if (legacy && !result.contains(u"wallet"_q)) {
		result.push_back(u"wallet"_q);
	}
	return result;
}

void SetNavigation(const QStringList &items) {
	const auto bytes = QByteArray("v2:") + items.join(u',').toUtf8();
	App().settings().writePref<QByteArray>(kNavigation, bytes);
	App().saveSettingsDelayed();
}

int AvatarCornerPercent() {
	return std::clamp(App().settings().readPref<int>(
		"telega.appearance.avatar-corner-percent", 50), 0, 50);
}

void SetAvatarCornerPercent(int percent) {
	App().settings().writePref<int>("telega.appearance.avatar-corner-percent",
		std::clamp(percent, 0, 50));
	App().saveSettingsDelayed();
}

void InitAvatarAppearance() {
	Ui::InitUserpicRounding(AvatarCornerPercent() / 100.,
		DisplayEnabled(DisplayOption::UniformAvatarRadius),
		DisplayEnabled(DisplayOption::RoundedContactAvatars));
}

AppIconAppearance ApplicationIconAppearance() {
	auto &settings = App().settings();
	return {
		.style = AppIconStyle(std::clamp(
			settings.readPref<int>(kAppIcon, 0), 0, int(AppIconStyle::Blue))),
		.roundedSquare = settings.readPref<bool>(kAppIconSquare, false),
	};
}

AppIconAppearance ActiveApplicationIconAppearance() {
	return AppIconAtStartup();
}

void SetApplicationIconAppearance(AppIconAppearance appearance) {
	auto &settings = App().settings();
	settings.writePref<int>(kAppIcon, int(appearance.style));
	settings.writePref<bool>(kAppIconSquare, appearance.roundedSquare);
	App().saveSettings();
}

void InitApplicationIconAppearance() {
	AppIconAtStartup() = ApplicationIconAppearance();
}

DownloadMode TransferDownloadMode() {
	return DownloadMode(std::clamp(App().settings().readPref<int>(kDownloadMode, 0), 0, 2));
}

void SetTransferDownloadMode(DownloadMode mode) {
	App().settings().writePref<int>(kDownloadMode, int(mode));
	App().saveSettingsDelayed();
}

QString FormatTime(const QTime &time) {
	const auto locale = QLocale();
	if (!DisplayEnabled(DisplayOption::TimeWithSeconds)) {
		return locale.toString(time, QLocale::ShortFormat);
	}
	auto format = locale.timeFormat(QLocale::ShortFormat);
	if (!format.contains(u's')) {
		const auto minute = format.lastIndexOf(u'm');
		if (minute >= 0) {
			format.insert(minute + 1, u":ss"_q);
		}
	}
	return locale.toString(time, format);
}

bool IsWideChannelPost(not_null<const HistoryItem*> item) {
	return item->isPost()
		&& item->history()->peer->isBroadcast()
		&& DisplayEnabled(DisplayOption::WideChannelPosts);
}

bool UseGoogleTranslation() {
	return GoogleTranslationEnabled().current();
}

rpl::producer<bool> UseGoogleTranslationValue() {
	return GoogleTranslationEnabled().value();
}

void SetUseGoogleTranslation(bool enabled) {
	Set(enabled, kGoogleTranslation, GoogleTranslationEnabled());
}

bool HideStories() {
	return StoriesHidden().current();
}

rpl::producer<bool> HideStoriesValue() {
	return StoriesHidden().value();
}

void SetHideStories(bool enabled) {
	Set(enabled, kHideStories, StoriesHidden());
}

bool CommaAfterMention() {
	return MentionCommaEnabled().current();
}

rpl::producer<bool> CommaAfterMentionValue() {
	return MentionCommaEnabled().value();
}

void SetCommaAfterMention(bool enabled) {
	Set(enabled, kCommaAfterMention, MentionCommaEnabled());
}

bool HideCopyToast() {
	return CopyToastHidden().current();
}

rpl::producer<bool> HideCopyToastValue() {
	return CopyToastHidden().value();
}

void SetHideCopyToast(bool enabled) {
	Set(enabled, kHideCopyToast, CopyToastHidden());
}

bool HideBubbleTails() {
	return BubbleTailsHidden().current();
}

rpl::producer<bool> HideBubbleTailsValue() {
	return BubbleTailsHidden().value();
}

void SetHideBubbleTails(bool enabled) {
	Set(enabled, kHideBubbleTails, BubbleTailsHidden());
}

bool HideFastShare() {
	return FastShareHidden().current();
}

rpl::producer<bool> HideFastShareValue() {
	return FastShareHidden().value();
}

void SetHideFastShare(bool enabled) {
	Set(enabled, kHideFastShare, FastShareHidden());
}

bool PlainReplyPreviews() {
	return ReplyPreviewsPlain().current();
}

rpl::producer<bool> PlainReplyPreviewsValue() {
	return ReplyPreviewsPlain().value();
}

void SetPlainReplyPreviews(bool enabled) {
	Set(enabled, kPlainReplyPreviews, ReplyPreviewsPlain());
}

bool PauseVideoInBackground() {
	return BackgroundVideoPaused().current();
}

rpl::producer<bool> PauseVideoInBackgroundValue() {
	return BackgroundVideoPaused().value();
}

void SetPauseVideoInBackground(bool enabled) {
	Set(enabled, kPauseVideoInBackground, BackgroundVideoPaused());
}

bool PreferOriginalVideo() {
	return OriginalVideoPreferred().current();
}

rpl::producer<bool> PreferOriginalVideoValue() {
	return OriginalVideoPreferred().value();
}

void SetPreferOriginalVideo(bool enabled) {
	Set(enabled, kPreferOriginalVideo, OriginalVideoPreferred());
}

bool DefaultPhotoHd() {
	return PhotoHdDefault().current();
}

rpl::producer<bool> DefaultPhotoHdValue() {
	return PhotoHdDefault().value();
}

void SetDefaultPhotoHd(bool enabled) {
	Set(enabled, kDefaultPhotoHd, PhotoHdDefault());
}

int VideoSeekStep() {
	const auto stored = App().settings().readPref<int>(kVideoSeekStep, 5);
	return ValidVideoSeekStep(stored) ? stored : 5;
}

void SetVideoSeekStep(int seconds) {
	if (ValidVideoSeekStep(seconds) && seconds != VideoSeekStep()) {
		App().settings().writePref<int>(kVideoSeekStep, seconds);
		App().saveSettingsDelayed();
	}
}

rpl::producer<bool> ProtectMainWindowValue() {
	return WindowProtected().value();
}

void SetProtectMainWindow(bool enabled) {
	Set(enabled, kProtectWindow, WindowProtected());
}

} // namespace Core::Telega
