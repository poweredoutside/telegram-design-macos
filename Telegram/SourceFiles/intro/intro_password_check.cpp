/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_password_check.h"

#include "intro/intro_widget.h"
#include "core/core_cloud_password.h"
#include "core/core_telega_preferences.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/abstract_box.h"
#include "boxes/passcode_box.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "intro/intro_signup.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "main/main_account.h"
#include "base/random.h"
#include "styles/style_intro.h"
#include "styles/style_widgets.h"

namespace Intro {
namespace details {
namespace {

const style::InputField &PasswordFieldStyle() {
	if (!Core::Telega::UseWindows11Style()) {
		return st::introPassword;
	}
	static const auto result = [] {
		auto result = st::introPassword;
		const auto s = [](int size) { return style::ConvertScale(size); };
		result.heightMin = s(34);
		result.borderRadius = s(4);
		result.textAlign = style::al_left;
		result.textMargins = QMargins(s(10), 0, s(10), 0);
		result.textBg = result.textBgActive = st::boxSearchBg;
		return result;
	}();
	return result;
}

} // namespace

PasswordCheckWidget::PasswordCheckWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data, false, Core::Telega::UseWindows11Style())
, _passwordState(getData()->pwdState)
, _pwdField(this, PasswordFieldStyle(), tr::lng_signin_password())
, _pwdHint(this, st::introPasswordHint)
, _codeField(this, PasswordFieldStyle(), tr::lng_signin_code())
, _toRecover(this, tr::lng_signin_recover(tr::now))
, _toPassword(this, tr::lng_signin_try_password(tr::now)) {
	Expects(_passwordState.hasPassword);
	if (Core::Telega::UseWindows11Style()) {
		setErrorCentered(true);
		_illustration.create(this);
		const auto size = style::ConvertScale(94);
		_illustration->resize(size, size);
		const auto icon = Lottie::MakeIcon({
			.name = u"cloud_password/password_input"_q,
			.sizeOverride = QSize(size, size),
		});
		const auto image = icon ? icon->frame() : QImage();
		_illustration->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(_illustration.data());
			p.drawImage(_illustration->rect(), image);
		}, _illustration->lifetime());
	}

	Lang::Updated(
	) | rpl::on_next([=] {
		refreshLang();
	}, lifetime());

	_toRecover->addClickHandler([=] { toRecover(); });
	_toPassword->addClickHandler([=] { toPassword(); });
	connect(_pwdField, &Ui::PasswordInput::changed, [=] { hideError(); });
	_codeField->changes(
	) | rpl::on_next([=] {
		hideError();
	}, _codeField->lifetime());
	_codeField->submits(
	) | rpl::on_next([=] {
		submit();
	}, _codeField->lifetime());

	setTitleText(tr::lng_signin_title());
	updateDescriptionText();

	if (_passwordState.hint.isEmpty()) {
		_pwdHint->hide();
	} else {
		_pwdHint->setText(tr::lng_signin_hint(
			tr::now,
			lt_password_hint,
			_passwordState.hint));
	}
	_codeField->hide();
	_toPassword->hide();

	setMouseTracking(true);
}

void PasswordCheckWidget::refreshLang() {
	if (_toRecover) {
		_toRecover->setText(tr::lng_signin_recover(tr::now));
	}
	if (_toPassword) {
		_toPassword->setText(
			tr::lng_signin_try_password(tr::now));
	}
	if (!_passwordState.hint.isEmpty()) {
		_pwdHint->setText(tr::lng_signin_hint(
			tr::now,
			lt_password_hint,
			_passwordState.hint));
	}
	updateControlsGeometry();
}

int PasswordCheckWidget::errorTop() const {
	if (_illustration) {
		return _toRecover->y() + _toRecover->height() + style::ConvertScale(8);
	}
	return contentTop() + st::introErrorBelowLinkTop;
}

void PasswordCheckWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateControlsGeometry();
}

void PasswordCheckWidget::updateControlsGeometry() {
	if (_illustration) {
		const auto s = [](int size) { return style::ConvertScale(size); };
		const auto top = contentTop();
		_illustration->moveToLeft((width() - _illustration->width()) / 2, top);
		_pwdField->moveToLeft(contentLeft(), top + s(200));
		_codeField->moveToLeft(contentLeft(), _pwdField->y());
		_pwdHint->moveToLeft(contentLeft(), _pwdField->y() + _pwdField->height());
		const auto linkTop = nextButtonTop() + st::introNextButton.height + s(12);
		_toRecover->moveToLeft((width() - _toRecover->width()) / 2, linkTop);
		_toPassword->moveToLeft((width() - _toPassword->width()) / 2, linkTop);
		return;
	}
	_pwdField->moveToLeft(contentLeft(), contentTop() + st::introPasswordTop);
	_pwdHint->moveToLeft(contentLeft() + st::buttonRadius, contentTop() + st::introPasswordHintTop);
	_codeField->moveToLeft(contentLeft(), contentTop() + st::introStepFieldTop);
	auto linkTop = _codeField->y() + _codeField->height() + st::introLinkTop;
	_toRecover->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
	_toPassword->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
}

void PasswordCheckWidget::setInnerFocus() {
	if (_pwdField->isHidden()) {
		_codeField->setFocusFast();
	} else {
		_pwdField->setFocusFast();
	}
}

void PasswordCheckWidget::activate() {
	if (_pwdField->isHidden() && _codeField->isHidden()) {
		Step::activate();
		_pwdField->show();
		_pwdHint->show();
		_toRecover->show();
	}
	setInnerFocus();
}

void PasswordCheckWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
}

void PasswordCheckWidget::pwdSubmitDone(
		bool recover,
		const MTPauth_Authorization &result) {
	_sentRequest = 0;
	if (recover) {
		cSetPasswordRecovered(true);
	}
	finish(result);
}

void PasswordCheckWidget::pwdSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		_sentRequest = 0;
		showError(tr::lng_flood_error());
		_pwdField->showError();
		return;
	}

	_sentRequest = 0;
	const auto &type = error.type();
	if (type == u"PASSWORD_HASH_INVALID"_q
		|| type == u"SRP_PASSWORD_CHANGED"_q) {
		showError(tr::lng_signin_bad_password());
		_pwdField->selectAll();
		_pwdField->showError();
	} else if (type == u"PASSWORD_EMPTY"_q
		|| type == u"AUTH_KEY_UNREGISTERED"_q) {
		goBack();
	} else if (type == u"SRP_ID_INVALID"_q) {
		handleSrpIdInvalid();
	} else if (!MTP::IgnoreError(error)) {
		showError(rpl::single(type));
		_pwdField->setFocus();
	}
}

void PasswordCheckWidget::handleSrpIdInvalid() {
	const auto now = crl::now();
	if (_lastSrpIdInvalidTime > 0
		&& now - _lastSrpIdInvalidTime < Core::kHandleSrpIdInvalidTimeout) {
		_passwordState.mtp.request.id = 0;
		showError(rpl::single(Lang::Hard::ServerError()));
	} else {
		_lastSrpIdInvalidTime = now;
		requestPasswordData();
	}
}

void PasswordCheckWidget::checkPasswordHash() {
	if (_passwordState.mtp.request.id) {
		passwordChecked();
	} else {
		requestPasswordData();
	}
}

void PasswordCheckWidget::requestPasswordData() {
	api().request(base::take(_sentRequest)).cancel();
	_sentRequest = api().request(
		MTPaccount_GetPassword()
	).done([=](const MTPaccount_Password &result) {
		_sentRequest = 0;
		result.match([&](const MTPDaccount_password &data) {
			base::RandomAddSeed(bytes::make_span(data.vsecure_random().v));
			_passwordState = Core::ParseCloudPasswordState(data);
			passwordChecked();
		});
	}).send();
}

void PasswordCheckWidget::passwordChecked() {
	if (!_passwordState.mtp.request) {
		if (_passwordState.mtp.unknownAlgorithm) {
			// Switched to an algorithm this build cannot compute.
			return serverError();
		}
		// The password was removed after this step was created, which is the
		// same situation the PASSWORD_EMPTY error reports.
		return goBack();
	}
	const auto check = Core::ComputeCloudPasswordCheck(
		_passwordState.mtp.request,
		_passwordHash);
	if (!check) {
		return serverError();
	}
	_passwordState.mtp.request.id = 0;
	_sentRequest = api().request(
		MTPauth_CheckPassword(check.result)
	).done([=](const MTPauth_Authorization &result) {
		pwdSubmitDone(false, result);
	}).fail([=](const MTP::Error &error) {
		pwdSubmitFail(error);
	}).handleFloodErrors().send();
}

void PasswordCheckWidget::serverError() {
	showError(rpl::single(Lang::Hard::ServerError()));
}

void PasswordCheckWidget::codeSubmitDone(
		const QString &code,
		const MTPBool &result) {
	auto fields = PasscodeBox::CloudFields::From(_passwordState);
	fields.fromRecoveryCode = code;
	fields.hasRecovery = false;
	fields.mtp.curRequest = {};
	fields.hasPassword = false;
	auto box = Box<PasscodeBox>(&api().instance(), nullptr, fields);
	const auto boxShared = std::make_shared<base::weak_qptr<PasscodeBox>>();

	box->newAuthorization(
	) | rpl::on_next([=](const MTPauth_Authorization &result) {
		if (boxShared) {
			(*boxShared)->closeBox();
		}
		pwdSubmitDone(true, result);
	}, lifetime());

	*boxShared = Ui::show(std::move(box));
}

void PasswordCheckWidget::codeSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		showError(tr::lng_flood_error());
		_codeField->showError();
		return;
	}

	_sentRequest = 0;
	const auto &type = error.type();
	if (type == u"PASSWORD_EMPTY"_q
		|| type == u"AUTH_KEY_UNREGISTERED"_q) {
		goBack();
	} else if (type == u"PASSWORD_RECOVERY_NA"_q) {
		recoverStartFail(error);
	} else if (type == u"PASSWORD_RECOVERY_EXPIRED"_q) {
		_emailPattern = QString();
		toPassword();
	} else if (type == u"CODE_INVALID"_q) {
		showError(tr::lng_signin_wrong_code());
		_codeField->selectAll();
		_codeField->showError();
	} else if (!MTP::IgnoreError(error)) {
		showError(rpl::single(type));
		_codeField->setFocus();
	}
}

void PasswordCheckWidget::recoverStarted(const MTPauth_PasswordRecovery &result) {
	_emailPattern = qs(result.c_auth_passwordRecovery().vemail_pattern());
	updateDescriptionText();
}

void PasswordCheckWidget::recoverStartFail(const MTP::Error &error) {
	_pwdField->show();
	_pwdHint->show();
	_codeField->hide();
	_pwdField->setFocus();
	updateDescriptionText();
	update();
	hideError();
}

void PasswordCheckWidget::toRecover() {
	if (_passwordState.hasRecovery) {
		if (_sentRequest) {
			api().request(base::take(_sentRequest)).cancel();
		}
		hideError();
		_toRecover->hide();
		_toPassword->show();
		_pwdField->hide();
		_pwdHint->hide();
		_pwdField->setText(QString());
		_codeField->show();
		_codeField->setFocus();
		updateDescriptionText();
		if (_emailPattern.isEmpty()) {
			api().request(
				MTPauth_RequestPasswordRecovery()
			).done([=](const MTPauth_PasswordRecovery &result) {
				recoverStarted(result);
			}).fail([=](const MTP::Error &error) {
				recoverStartFail(error);
			}).send();
		}
	} else {
		const auto box = Ui::show(
			Ui::MakeInformBox(tr::lng_signin_no_email_forgot()));
		box->boxClosing(
		) | rpl::on_next([=] {
			showReset();
		}, box->lifetime());
	}
}

void PasswordCheckWidget::toPassword() {
	const auto box = Ui::show(
		Ui::MakeInformBox(tr::lng_signin_cant_email_forgot()));
	box->boxClosing(
	) | rpl::on_next([=] {
		showReset();
	}, box->lifetime());
}

void PasswordCheckWidget::showReset() {
	if (_sentRequest) {
		api().request(base::take(_sentRequest)).cancel();
	}
	_toRecover->show();
	_toPassword->hide();
	_pwdField->show();
	_pwdHint->show();
	_codeField->hide();
	_codeField->setText(QString());
	_pwdField->setFocus();
	showResetButton();
	updateDescriptionText();
	update();
}

void PasswordCheckWidget::updateDescriptionText() {
	auto pwdHidden = _pwdField->isHidden();
	auto emailPattern = _emailPattern;
	setDescriptionText(pwdHidden
		? tr::lng_signin_recover_desc(
			lt_email,
			rpl::single(Ui::Text::WrapEmailPattern(emailPattern)),
			tr::marked)
		: tr::lng_signin_desc(tr::marked));
}

void PasswordCheckWidget::submit() {
	if (_sentRequest) {
		return;
	}
	if (_pwdField->isHidden()) {
		auto code = _codeField->getLastText().trimmed();
		if (code.isEmpty()) {
			_codeField->showError();
			return;
		}
		const auto send = crl::guard(this, [=] {
			_sentRequest = api().request(MTPauth_CheckRecoveryPassword(
				MTP_string(code)
			)).done([=](const MTPBool &result) {
				codeSubmitDone(code, result);
			}).fail([=](const MTP::Error &error) {
				codeSubmitFail(error);
			}).handleFloodErrors().send();
		});

		if (_passwordState.notEmptyPassport) {
			const auto confirmed = [=](Fn<void()> &&close) {
				send();
				close();
			};
			Ui::show(Ui::MakeConfirmBox({
				.text = tr::lng_cloud_password_passport_losing(),
				.confirmed = confirmed,
				.confirmText = tr::lng_continue(),
			}));
		} else {
			send();
		}
	} else {
		hideError();

		if (!_passwordState.mtp.request) {
			return serverError();
		}
		const auto password = _pwdField->getLastText().toUtf8();
		_passwordHash = Core::ComputeCloudPasswordHash(
			_passwordState.mtp.request.algo,
			bytes::make_span(password));
		checkPasswordHash();
	}
}

rpl::producer<QString> PasswordCheckWidget::nextButtonText() const {
	return _illustration ? tr::lng_box_ok() : tr::lng_intro_submit();
}

int PasswordCheckWidget::nextButtonTop() const {
	return _illustration
		? contentTop() + style::ConvertScale(_passwordState.hint.isEmpty() ? 245 : 265)
		: Step::nextButtonTop();
}

} // namespace details
} // namespace Intro
