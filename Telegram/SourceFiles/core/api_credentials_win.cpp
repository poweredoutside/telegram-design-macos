#include "core/api_credentials.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>

#include <Windows.h>
#include <wincrypt.h>

namespace Core::ApiCredentials {
namespace {

struct Credentials {
	int id = 0;
	QByteArray hash;
};

Credentials Current;

[[nodiscard]] Credentials Parse(const QString &id, const QString &hash) {
	static const auto IdPattern = QRegularExpression(u"\\A[1-9][0-9]{0,9}\\z"_q);
	static const auto HashPattern = QRegularExpression(u"\\A[0-9a-fA-F]{32}\\z"_q);
	const auto parsed = id.toInt();
	return (parsed > 0 && IdPattern.match(id).hasMatch()
			&& HashPattern.match(hash).hasMatch())
		? Credentials{ parsed, hash.toLatin1() }
		: Credentials();
}

[[nodiscard]] Credentials Load(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) {
		return {};
	}
	auto encrypted = file.read(4097);
	if (encrypted.isEmpty() || encrypted.size() > 4096) {
		return {};
	}
	auto input = DATA_BLOB{
		DWORD(encrypted.size()),
		reinterpret_cast<BYTE*>(encrypted.data()),
	};
	auto output = DATA_BLOB{};
	if (!CryptUnprotectData(
			&input, nullptr, nullptr, nullptr, nullptr,
			CRYPTPROTECT_UI_FORBIDDEN, &output)) {
		return {};
	}
	const auto plain = QByteArrayView(
		reinterpret_cast<const char*>(output.pbData), output.cbData);
	const auto split = plain.indexOf('\n');
	const auto result = (split > 0 && split <= 10 && plain.size() == split + 33)
		? Parse(
			QString::fromLatin1(plain.first(split)),
			QString::fromLatin1(plain.sliced(split + 1)))
		: Credentials();
	SecureZeroMemory(output.pbData, output.cbData);
	LocalFree(output.pbData);
	return result;
}

[[nodiscard]] bool Save(const QString &path, const Credentials &value) {
	auto plain = QByteArray::number(value.id) + '\n' + value.hash;
	auto input = DATA_BLOB{
		DWORD(plain.size()),
		reinterpret_cast<BYTE*>(plain.data()),
	};
	auto output = DATA_BLOB{};
	const auto protectedData = CryptProtectData(
		&input, L"Telega Desktop", nullptr, nullptr, nullptr,
		CRYPTPROTECT_UI_FORBIDDEN, &output);
	SecureZeroMemory(plain.data(), plain.size());
	if (!protectedData) {
		return false;
	}
	auto file = QSaveFile(path);
	const auto written = file.open(QIODevice::WriteOnly)
		&& (file.write(reinterpret_cast<const char*>(output.pbData), output.cbData)
			== output.cbData)
		&& file.commit();
	LocalFree(output.pbData);
	return written;
}

} // namespace

void Prepare(QObject *context, Fn<void(bool)> done) {
	const auto path = cWorkingDir() + u"tdata/telega-api.dat"_q;
	Current = Load(path);
	if (Current.id) {
		done(true);
		return;
	}

	const auto russian = QLocale::system().language() == QLocale::Russian;
	const auto dialog = new QDialog();
	dialog->setAttribute(Qt::WA_QuitOnClose, false);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(u"Telega Desktop"_q);
	const auto layout = new QFormLayout(dialog);
	const auto instructions = new QLabel(russian
		? u"Введите данные своего приложения с <a href='https://my.telegram.org/apps'>my.telegram.org</a>."_q
		: u"Enter your own application credentials from <a href='https://my.telegram.org/apps'>my.telegram.org</a>."_q);
	instructions->setOpenExternalLinks(true);
	instructions->setWordWrap(true);
	layout->addRow(instructions);
	const auto id = new QLineEdit();
	const auto hash = new QLineEdit();
	hash->setEchoMode(QLineEdit::Password);
	layout->addRow(u"api_id"_q, id);
	layout->addRow(u"api_hash"_q, hash);
	const auto error = new QLabel(QFile::exists(path)
		? (russian
			? u"Не удалось прочитать сохранённые данные. Введите их заново."_q
			: u"Could not read the saved credentials. Please enter them again."_q)
		: QString());
	error->setWordWrap(true);
	layout->addRow(error);
	const auto buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(russian
		? u"Продолжить"_q
		: u"Continue"_q);
	buttons->button(QDialogButtonBox::Cancel)->setText(russian
		? u"Отмена"_q
		: u"Cancel"_q);
	layout->addRow(buttons);
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
		auto value = Parse(id->text().trimmed(), hash->text().trimmed());
		if (!value.id) {
			error->setText(russian
				? u"Укажите корректный api_id и api_hash (32 шестнадцатеричных символа)."_q
				: u"Enter a valid api_id and a 32-character hexadecimal api_hash."_q);
		} else if (!Save(path, value)) {
			error->setText(russian
				? u"Не удалось сохранить данные в профиле Telega Desktop."_q
				: u"Could not save the credentials in the Telega Desktop profile."_q);
		} else {
			Current = std::move(value);
			hash->clear();
			dialog->accept();
		}
	});
	QObject::connect(dialog, &QDialog::finished, context, [done = std::move(done)](int result) {
		done(result == QDialog::Accepted);
	}, Qt::QueuedConnection);
	QObject::connect(context, &QObject::destroyed, dialog, &QDialog::close);
	dialog->resize(430, dialog->sizeHint().height());
	dialog->open();
}

int Id() {
	return Current.id;
}

const char *Hash() {
	return Current.hash.constData();
}

int CrashReportId() {
	return 0;
}

} // namespace Core::ApiCredentials
