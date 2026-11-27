#include "info/stories/info_stories_publish.h"

#include "apiwrap.h"
#include "base/random.h"
#include "core/file_utilities.h"
#include "data/data_session.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"

#include <QtCore/QFileInfo>

namespace Info::Stories {
namespace {

constexpr auto kMaxStoryBytes = 30 * 1024 * 1024;

enum class Audience { Contacts, CloseFriends, Everyone };

QString Text(const QString &ru, const QString &en) {
	return Lang::GetInstance().id().startsWith(u"ru"_q) ? ru : en;
}

MTPInputPrivacyRule PrivacyRule(Audience audience) {
	switch (audience) {
	case Audience::Contacts: return MTP_inputPrivacyValueAllowContacts();
	case Audience::CloseFriends: return MTP_inputPrivacyValueAllowCloseFriends();
	case Audience::Everyone: return MTP_inputPrivacyValueAllowAll();
	}
	Unexpected("Story audience.");
}

QString ErrorText(const MTP::Error &error) {
	const auto &type = error.type();
	if (type == u"PREMIUM_ACCOUNT_REQUIRED"_q) {
		return Text(u"Для публикации истории нужен Telegram Premium."_q,
			u"Telegram Premium is required to post a story."_q);
	} else if (type == u"STORIES_TOO_MUCH"_q
		|| type.startsWith(u"STORY_SEND_FLOOD"_q)) {
		return Text(u"Лимит историй исчерпан. Попробуйте позже."_q,
			u"You have reached the story limit. Try again later."_q);
	}
	return Text(u"Не удалось опубликовать историю."_q,
		u"Could not publish the story."_q);
}

struct Transfer {
	explicit Transfer(not_null<Main::Session*> session)
	: session(base::make_weak(session)) {
	}

	~Transfer() {
		uploadLifetime.destroy();
		if (const auto strong = session.get()) {
			if (requestId) {
				strong->api().request(requestId).cancel();
			}
			if (uploadId) {
				strong->uploader().cancel(uploadId);
			}
		}
	}

	base::weak_ptr<Main::Session> session;
	rpl::lifetime uploadLifetime;
	FullMsgId uploadId;
	mtpRequestId requestId = 0;
	uint64 randomId = base::RandomValue<uint64>();
};

bool PrepareStory(FileLoadTask &task) {
	task.process({ .generateGoodThumbnail = false });
	const auto &file = task.peekResult();
	if (!file || file->filesize <= 0 || file->filesize > kMaxStoryBytes) {
		return false;
	}
	if (file->type == SendMediaType::Photo) {
		const auto full = file->photoThumbs.find('y');
		return full != end(file->photoThumbs)
			&& full->second.image.width() < full->second.image.height()
			&& file->partssize > 0
			&& file->partssize <= kMaxStoryBytes;
	}
	if (file->filemime != u"video/mp4"_q
		|| file->videoSource
		|| file->document.type() != mtpc_document) {
		return false;
	}
	const auto &attributes = file->document.c_document().vattributes().v;
	return ranges::any_of(attributes, [](const MTPDocumentAttribute &attribute) {
		if (attribute.type() != mtpc_documentAttributeVideo) {
			return false;
		}
		const auto &video = attribute.c_documentAttributeVideo();
		return video.vw().v > 0
			&& video.vw().v < video.vh().v
			&& video.vduration().v > 0.
			&& video.vduration().v <= 60.;
	});
}

MTPInputMedia StoryMedia(
		const FilePrepareResult &file,
		const Api::RemoteFileInfo &uploaded) {
	if (file.type == SendMediaType::Photo) {
		return MTP_inputMediaUploadedPhoto(
			MTP_flags(0),
			uploaded.file,
			MTPVector<MTPInputDocument>(),
			MTPint(),
			MTPInputDocument());
	}
	using Flag = MTPDinputMediaUploadedDocument::Flag;
	return MTP_inputMediaUploadedDocument(
		MTP_flags(uploaded.thumb ? Flag::f_thumb : Flag()),
		uploaded.file,
		uploaded.thumb.value_or(MTPInputFile()),
		MTP_string(file.filemime),
		file.document.c_document().vattributes(),
		MTPVector<MTPInputDocument>(),
		MTPInputPhoto(),
		MTPint(),
		MTPint());
}

void PublishBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		QString path) {
	const auto session = &controller->session();
	const auto state = box->lifetime().make_state<Transfer>(session);
	box->setTitle(rpl::single(NewStoryLabel()));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		QFileInfo(path).fileName(),
		st::boxLabel));
	const auto caption = box->addRow(object_ptr<Ui::InputField>(
		box, st::defaultInputField, tr::lng_photo_caption()));
	const auto limit = session->premium()
		? session->appConfig().get(u"story_caption_length_limit_premium"_q, 2048)
		: session->appConfig().get(u"story_caption_length_limit_default"_q, 200);
	caption->setMaxLength(std::max(limit, 1));

	const auto audience = std::make_shared<Ui::RadioenumGroup<Audience>>(
		Audience::Contacts);
	const auto addAudience = [&](Audience value, const QString &label) {
		return box->addRow(object_ptr<Ui::Radioenum<Audience>>(
			box, audience, value, label));
	};
	const auto choices = std::array{
		addAudience(Audience::Contacts, tr::lng_edit_privacy_contacts(tr::now)),
		addAudience(
			Audience::CloseFriends,
			tr::lng_edit_privacy_close_friends(tr::now)),
		addAudience(Audience::Everyone, tr::lng_edit_privacy_everyone(tr::now)),
	};
	const auto pinned = box->addRow(object_ptr<Ui::Checkbox>(
		box, tr::lng_mediaview_save_to_profile(), false));
	const auto status = box->addRow(object_ptr<Ui::FlatLabel>(
		box, QString(), st::boxLabel));
	status->hide();
	const auto button = box->lifetime().make_state<QPointer<Ui::RoundButton>>();
	const auto setBusy = [=](bool busy) {
		(*button)->setDisabled(busy);
		caption->setDisabled(busy);
		pinned->setDisabled(busy);
		for (const auto choice : choices) {
			choice->setDisabled(busy);
		}
	};
	const auto fail = [=](QString text) {
		status->setText(std::move(text));
		status->show();
		setBusy(false);
	};
	const auto failRpc = [=](const MTP::Error &error) {
		state->requestId = 0;
		fail(ErrorText(error));
	};
	const auto publish = [=] {
		const auto strong = state->session.get();
		if (!strong || !*button || (*button)->isDisabled()) {
			return;
		}
		setBusy(true);
		status->setText(Text(u"Публикация…"_q, u"Publishing…"_q));
		status->show();
		const auto text = caption->getLastText().trimmed();
		const auto rule = PrivacyRule(audience->current());
		const auto pin = pinned->checked();
		state->requestId = strong->api().request(MTPstories_CanSendStory(
			MTP_inputPeerSelf()
		)).done(crl::guard(box, [=](const MTPstories_CanSendStoryCount &result) {
			state->requestId = 0;
			if (result.c_stories_canSendStoryCount().vcount_remains().v <= 0) {
				fail(Text(u"Лимит историй исчерпан."_q, u"Story limit reached."_q));
				return;
			}
			const auto task = std::make_shared<FileLoadTask>(FileLoadTask::Args{
				.session = session,
				.filepath = path,
				.type = SendMediaType::Photo,
				.to = { session->userPeerId(), {}, {}, {} },
			});
			const auto prepared = crl::guard(box, [=](bool valid) {
				const auto strong = state->session.get();
				if (!strong) {
					return;
				} else if (!valid) {
					fail(Text(
						u"Выберите вертикальное фото или MP4 до 60 секунд и 30 МБ."_q,
						u"Choose a vertical photo or an MP4 up to 60 seconds and 30 MB."_q));
					return;
				}
				const auto file = task->peekResult();
				state->uploadLifetime.destroy();
				state->uploadId = FullMsgId(
					PeerId(), strong->data().nextLocalMessageId());
				const auto id = state->uploadId;
				auto &uploader = strong->uploader();
				const auto photo = (file->type == SendMediaType::Photo);
				(photo ? uploader.photoFailed() : uploader.documentFailed()
				) | rpl::filter(rpl::mappers::_1 == id) | rpl::on_next([=] {
					state->uploadId = {};
					fail(Text(u"Не удалось загрузить файл."_q,
						u"Could not upload the file."_q));
				}, state->uploadLifetime);
				(photo ? uploader.photoReady() : uploader.documentReady()
				) | rpl::filter([=](const Storage::UploadedMedia &media) {
					return media.fullId == id;
				}) | rpl::take(1) | rpl::on_next([=](const Storage::UploadedMedia &media) {
					state->uploadId = {};
					using Flag = MTPstories_SendStory::Flag;
					const auto flags = (text.isEmpty() ? Flag() : Flag::f_caption)
						| (pin ? Flag::f_pinned : Flag());
					state->requestId = session->api().request(MTPstories_SendStory(
						MTP_flags(flags),
						MTP_inputPeerSelf(),
						StoryMedia(*file, media.info),
						MTPVector<MTPMediaArea>(),
						MTP_string(text),
						MTPVector<MTPMessageEntity>(),
						MTP_vector<MTPInputPrivacyRule>(QVector<MTPInputPrivacyRule>{ rule }),
						MTP_long(state->randomId),
						MTPint(),
						MTPInputPeer(),
						MTPint(),
						MTPVector<MTPint>(),
						MTPInputDocument()
					)).done(crl::guard(box, [=](const MTPUpdates &updates) {
						state->requestId = 0;
						session->api().applyUpdates(updates);
						controller->showToast(Text(
							u"История опубликована."_q, u"Story published."_q));
						box->closeBox();
					})).fail(crl::guard(box, failRpc)).send();
				}, state->uploadLifetime);
				uploader.upload(id, file);
			});
			crl::async([=] {
				const auto input = QFileInfo(path);
				const auto valid = input.isFile()
					&& input.size() > 0
					&& input.size() <= kMaxStoryBytes
					&& PrepareStory(*task);
				crl::on_main([=] { prepared(valid); });
			});
		})).fail(crl::guard(box, failRpc)).send();
	};
	*button = box->addButton(
		rpl::single(Text(u"Опубликовать"_q, u"Publish"_q)), publish);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

QString NewStoryLabel() {
	return Text(u"Новая история"_q, u"New story"_q);
}

void ShowPublishBox(not_null<Window::SessionController*> controller) {
	FileDialog::GetOpenPath(
		controller->widget().get(),
		NewStoryLabel(),
		FileDialog::PhotoVideoFilesFilter(),
		crl::guard(controller, [=](FileDialog::OpenResult &&result) {
			if (!result.paths.isEmpty()) {
				controller->show(Box(PublishBox, controller, result.paths.front()));
			}
		}));
}

} // namespace Info::Stories
