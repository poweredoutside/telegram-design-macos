#pragma once

#include <QtCore/QStringList>

class QTime;
class HistoryItem;

namespace Core::Telega {

enum class DisplayOption {
	ProfilePhotoDc,
	RelativeLastSeen,
	TimeWithSeconds,
	HideDefaultGreetingSticker,
	EditedIcon,
	ForwardCount,
	ExactViewCounts,
	RoundedContactAvatars,
	HideSidebarStatus,
	HideSenderMiniAvatars,
	AccentSenderNames,
	HidePrivateReactions,
	HideGroupReactions,
	HideChannelReactions,
	HideSendAs,
	QuoteReplies,
	CountCharacters,
	SelectServiceMessages,
	HideAllChatsFolder,
	PauseVoiceInBackground,
	PauseRoundVideoInBackground,
	ProfileMusicCover,
	UploadBoost,
	IgnorePeerThemes,
	HideTypingIndicator,
	OnlineSenderBadge,
	HideCopyPhotoAction,
	HideSaveMediaAction,
	RepeatMessageAction,
	MessageDetailsAction,
	PauseAutomaticChatReads,
	HideStoryViewReceipts,
	HideOnlineAnnouncements,
	InlineCalculator,
	QuickMessageActions,
	PreviewPollResults,
	SystemEmoji,
	KeepUnreadWhenSending,
	FilterVisualNoise,
	UniformAvatarRadius,
	VisibleGroupAuthors,
	SetOfflineAfterSending,
	HideSponsoredMessages,
	WideChannelPosts,
};

enum class ChannelBottomAction { Mute, Discussion, Hidden };
enum class StickerTimestamp { Automatic, Always, Hidden };
enum class DownloadMode { Standard, Fast, Ultra };
enum class AppIconStyle { Default, Accent, Dark, Light, Blue };
enum class InterfaceStyle { MacOS = 0, Windows = 2, Material3 = 3, Windows11 = 4 };

struct AppIconAppearance {
	AppIconStyle style = AppIconStyle::Default;
	bool roundedSquare = false;
};

[[nodiscard]] bool DisplayEnabled(DisplayOption option);
[[nodiscard]] rpl::producer<bool> DisplayValue(DisplayOption option);
void SetDisplayEnabled(DisplayOption option, bool enabled);
[[nodiscard]] bool SkipSendReadReceipt();
[[nodiscard]] QString FilterDisplayName(const QString &name);
[[nodiscard]] QString FormatTime(const QTime &time);
[[nodiscard]] bool IsWideChannelPost(not_null<const HistoryItem*> item);
[[nodiscard]] QString GifSearchBot();
[[nodiscard]] bool SetGifSearchBot(QString username);
[[nodiscard]] ChannelBottomAction BottomAction();
[[nodiscard]] rpl::producer<ChannelBottomAction> BottomActionValue();
void SetBottomAction(ChannelBottomAction action);
[[nodiscard]] StickerTimestamp StickerTime();
void SetStickerTime(StickerTimestamp mode);
[[nodiscard]] int StickerScale();
void SetStickerScale(int percent);
[[nodiscard]] QStringList DefaultNavigation();
[[nodiscard]] QStringList NavigationItems();
[[nodiscard]] QStringList Navigation();
void SetNavigation(const QStringList &items);
[[nodiscard]] DownloadMode TransferDownloadMode();
void SetTransferDownloadMode(DownloadMode mode);
[[nodiscard]] InterfaceStyle PreferredInterfaceStyle();
void SetPreferredInterfaceStyle(InterfaceStyle style);
void InitInterfaceStyle();
[[nodiscard]] bool UseMacOSStyle();
[[nodiscard]] bool UseMaterial3Style();
[[nodiscard]] bool UseWindows11Style();
[[nodiscard]] AppIconAppearance ApplicationIconAppearance();
[[nodiscard]] AppIconAppearance ActiveApplicationIconAppearance();
void SetApplicationIconAppearance(AppIconAppearance appearance);
void InitApplicationIconAppearance();
[[nodiscard]] int AvatarCornerPercent();
void SetAvatarCornerPercent(int percent);
void InitAvatarAppearance();

[[nodiscard]] bool UseGoogleTranslation();
[[nodiscard]] rpl::producer<bool> UseGoogleTranslationValue();
void SetUseGoogleTranslation(bool enabled);

[[nodiscard]] bool HideStories();
[[nodiscard]] rpl::producer<bool> HideStoriesValue();
void SetHideStories(bool enabled);

[[nodiscard]] bool CommaAfterMention();
[[nodiscard]] rpl::producer<bool> CommaAfterMentionValue();
void SetCommaAfterMention(bool enabled);

[[nodiscard]] bool HideCopyToast();
[[nodiscard]] rpl::producer<bool> HideCopyToastValue();
void SetHideCopyToast(bool enabled);

[[nodiscard]] bool HideBubbleTails();
[[nodiscard]] rpl::producer<bool> HideBubbleTailsValue();
void SetHideBubbleTails(bool enabled);

[[nodiscard]] bool HideFastShare();
[[nodiscard]] rpl::producer<bool> HideFastShareValue();
void SetHideFastShare(bool enabled);

[[nodiscard]] bool PlainReplyPreviews();
[[nodiscard]] rpl::producer<bool> PlainReplyPreviewsValue();
void SetPlainReplyPreviews(bool enabled);

[[nodiscard]] bool PauseVideoInBackground();
[[nodiscard]] rpl::producer<bool> PauseVideoInBackgroundValue();
void SetPauseVideoInBackground(bool enabled);

[[nodiscard]] bool PreferOriginalVideo();
[[nodiscard]] rpl::producer<bool> PreferOriginalVideoValue();
void SetPreferOriginalVideo(bool enabled);

[[nodiscard]] bool DefaultPhotoHd();
[[nodiscard]] rpl::producer<bool> DefaultPhotoHdValue();
void SetDefaultPhotoHd(bool enabled);

[[nodiscard]] int VideoSeekStep();
void SetVideoSeekStep(int seconds);

[[nodiscard]] rpl::producer<bool> ProtectMainWindowValue();
void SetProtectMainWindow(bool enabled);

} // namespace Core::Telega
