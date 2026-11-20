#include "platform/win/bundled_tg_ws_proxy.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "logs.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QRandomGenerator>
#include <QtCore/QSaveFile>
#include <QtCore/QUrl>
#include <QtNetwork/QHostAddress>

#include <algorithm>
#include <optional>

#include <windows.h>

namespace Platform {
namespace {

constexpr auto kProxyDataDir = u"TgWsProxy_data";
constexpr auto kProxyPortPref = "telega.proxy.port";
constexpr auto kProxyBufferPref = "telega.proxy.buffer-kib";
constexpr auto kProxyPoolPref = "telega.proxy.pool-size";
constexpr auto kProxyVerbosePref = "telega.proxy.verbose";

QDir ProxyDirectory() {
	return QDir(cWorkingDir() + u"tgwsproxy"_q);
}

QString ConfigurationPath(const QDir &folder) {
	return folder.filePath(QString::fromUtf16(kProxyDataDir) + u"/config.json"_q);
}

[[nodiscard]] std::optional<QJsonObject> ReadConfiguration(const QDir &folder) {
	auto file = QFile(ConfigurationPath(folder));
	if (!file.exists()) {
		return QJsonObject();
	}
	if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	return document.isObject() ? std::optional(document.object()) : std::nullopt;
}

BundledTgWsProxy::Options EffectiveOptions(const QJsonObject &config) {
	auto &settings = Core::App().settings();
	const auto defaults = BundledTgWsProxy::Options();
	const auto list = [&](const QString &key) {
		auto result = QStringList();
		for (const auto value : config.value(key).toArray()) {
			if (value.isString()) {
				result.push_back(value.toString());
			}
		}
		return result;
	};
	return {
		.port = std::clamp(settings.readPref<int>(kProxyPortPref,
			config.value(u"port"_q).toInt(defaults.port)), 1, 65535),
		.bufferKiB = std::clamp(settings.readPref<int>(kProxyBufferPref,
			config.value(u"buf_kb"_q).toInt(defaults.bufferKiB)), 4, 4096),
		.poolSize = std::clamp(settings.readPref<int>(kProxyPoolPref,
			config.value(u"pool_size"_q).toInt(defaults.poolSize)), 0, 32),
		.verbose = settings.readPref<bool>(kProxyVerbosePref,
			config.value(u"verbose"_q).toBool(defaults.verbose)),
		.cloudflareFallback = config.value(u"cfproxy"_q).toBool(true),
		.http2 = config.value(u"h2"_q).toBool(true),
		.secure = !config.value(u"no_secure"_q).toBool(false),
		.dcOverrides = config.contains(u"dc_ip"_q)
			? list(u"dc_ip"_q)
			: QStringList{ u"2:149.154.167.220"_q, u"4:149.154.167.220"_q },
		.cloudflareDomains = config.value(u"cfproxy_user_domain_enabled"_q).toBool(true)
			? list(u"cfproxy_user_domain"_q)
			: QStringList(),
		.workerDomains = config.value(u"cfproxy_worker_enabled"_q).toBool(true)
			? list(u"cfproxy_worker_domain"_q)
			: QStringList(),
	};
}

[[nodiscard]] QByteArrayView ProxyResource() {
	const auto resource = FindResourceW(nullptr, L"TGWSPROXY", RT_RCDATA);
	if (!resource) {
		return {};
	}
	const auto loaded = LoadResource(nullptr, resource);
	const auto data = loaded ? LockResource(loaded) : nullptr;
	return data
		? QByteArrayView(
			static_cast<const char*>(data),
			qsizetype(SizeofResource(nullptr, resource)))
		: QByteArrayView();
}

[[nodiscard]] bool SaveFile(const QString &path, QByteArrayView content) {
	auto file = QSaveFile(path);
	return file.open(QIODevice::WriteOnly)
		&& file.write(content.data(), content.size()) == content.size()
		&& file.commit();
}

[[nodiscard]] bool EnsureBinary(const QString &path) {
	const auto resource = ProxyResource();
	if (resource.isEmpty()) {
		return false;
	}
	{
		auto existing = QFile(path);
		if (existing.open(QIODevice::ReadOnly)
			&& existing.size() == resource.size()) {
			const auto mapped = existing.map(0, existing.size());
			if (mapped && QByteArrayView(
					reinterpret_cast<const char*>(mapped),
					resource.size()) == resource) {
				return true;
			}
		}
	}
	return SaveFile(path, resource);
}

[[nodiscard]] bool WriteConfiguration(
		const QDir &folder,
		QJsonObject config) {
	if (!folder.mkpath(QString::fromUtf16(kProxyDataDir))) {
		return false;
	}
	if (config.value(u"secret"_q).toString().isEmpty()) {
		auto secret = QString();
		for (auto index = 0; index != 4; ++index) {
			secret += u"%1"_q.arg(
				QRandomGenerator::system()->generate(), 8, 16, QChar(u'0'));
		}
		config.insert(u"secret"_q, secret);
	}
	config.insert(u"host"_q, u"127.0.0.1"_q);
	config.insert(u"check_updates"_q, false);
	config.insert(u"autostart"_q, false);
	return SaveFile(ConfigurationPath(folder),
		QJsonDocument(config).toJson(QJsonDocument::Compact));
}

[[nodiscard]] bool EnsureConfiguration(const QDir &folder) {
	const auto existing = ReadConfiguration(folder);
	if (!existing) {
		return false;
	}
	auto config = *existing;
	const auto options = EffectiveOptions(config);
	config.insert(u"port"_q, options.port);
	config.insert(u"buf_kb"_q, options.bufferKiB);
	config.insert(u"pool_size"_q, options.poolSize);
	config.insert(u"verbose"_q, options.verbose);
	return (config == *existing
		&& config.value(u"host"_q).toString() == u"127.0.0.1"_q
		&& config.value(u"check_updates"_q).isBool()
		&& !config.value(u"check_updates"_q).toBool()
		&& config.value(u"autostart"_q).isBool()
		&& !config.value(u"autostart"_q).toBool()
		&& !config.value(u"secret"_q).toString().isEmpty())
		|| WriteConfiguration(folder, std::move(config));
}

[[nodiscard]] HANDLE StartInJob(const QString &path, const QString &folder) {
	const auto job = CreateJobObjectW(nullptr, nullptr);
	if (!job) {
		return nullptr;
	}
	auto limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!SetInformationJobObject(
			job,
			JobObjectExtendedLimitInformation,
			&limits,
			sizeof(limits))) {
		CloseHandle(job);
		return nullptr;
	}
	const auto nativePath = QDir::toNativeSeparators(path).toStdWString();
	const auto nativeFolder = QDir::toNativeSeparators(folder).toStdWString();
	auto command = L'"' + nativePath + L"\" --portable";
	auto startup = STARTUPINFOW();
	startup.cb = sizeof(startup);
	auto process = PROCESS_INFORMATION();
	if (!CreateProcessW(
			nativePath.c_str(),
			command.data(),
			nullptr,
			nullptr,
			FALSE,
			CREATE_SUSPENDED | CREATE_NO_WINDOW,
			nullptr,
			nativeFolder.c_str(),
			&startup,
			&process)) {
		CloseHandle(job);
		return nullptr;
	}
	const auto started = AssignProcessToJobObject(job, process.hProcess)
		&& ResumeThread(process.hThread) != DWORD(-1);
	if (!started) {
		TerminateProcess(process.hProcess, 1);
		CloseHandle(job);
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return started ? job : nullptr;
}

} // namespace

BundledTgWsProxy::~BundledTgWsProxy() {
	stop();
}

void BundledTgWsProxy::stop() {
	if (_job) {
		CloseHandle(_job);
		_job = nullptr;
	}
}

bool BundledTgWsProxy::running() const {
	auto info = JOBOBJECT_BASIC_ACCOUNTING_INFORMATION();
	return _job && QueryInformationJobObject(
		_job, JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr)
		&& info.ActiveProcesses > 0;
}

bool BundledTgWsProxy::Available() {
	return !ProxyResource().isEmpty();
}

MTP::ProxyData BundledTgWsProxy::start() {
	if (const auto external = OpenMutexW(
			SYNCHRONIZE,
			FALSE,
			L"Local\\TgWsProxy_SingleInstance")) {
		CloseHandle(external);
		return {};
	}
	const auto folder = ProxyDirectory();
	const auto path = folder.filePath(u"TgWsProxy.exe"_q);
	const auto config = (Available() && EnsureConfiguration(folder) && EnsureBinary(path))
		? ReadConfiguration(folder)
		: std::nullopt;
	if (config) {
		auto proxy = MTP::ProxyData{
			.type = MTP::ProxyData::Type::Mtproto,
			.host = u"127.0.0.1"_q,
			.port = uint32(EffectiveOptions(*config).port),
			.password = u"dd"_q + config->value(u"secret"_q).toString(),
		};
		if (!proxy.valid()) {
			return {};
		}
		_job = StartInJob(path, folder.absolutePath());
		if (_job) {
			return proxy;
		}
	}
	LOG(("TgWsProxy: could not start the bundled proxy."));
	return {};
}

bool BundledTgWsProxy::StartAutomatically() {
	const auto fallback = QDir(cExeDir() + u"tgwsproxy.auto"_q).exists();
	return Core::App().settings().readPref<bool>("telega.proxy.start", fallback);
}

void BundledTgWsProxy::SetStartAutomatically(bool enabled) {
	Core::App().settings().writePref<bool>("telega.proxy.start", enabled);
	Core::App().saveSettingsDelayed();
}

BundledTgWsProxy::Options BundledTgWsProxy::Configuration() {
	return EffectiveOptions(ReadConfiguration(ProxyDirectory()).value_or(
		QJsonObject()));
}

bool BundledTgWsProxy::SetConfiguration(Options options) {
	if (options.dcOverrides.size() > 32 || options.cloudflareDomains.size() > 32
		|| options.workerDomains.size() > 32) {
		return false;
	}
	for (const auto &entry : options.dcOverrides) {
		const auto colon = entry.indexOf(u':');
		auto valid = false;
		const auto dc = entry.left(colon).toInt(&valid);
		const auto raw = entry.mid(colon + 1);
		const auto address = QHostAddress(raw);
		if (colon < 1 || !valid || dc < 1 || dc > 999
			|| address.protocol() != QAbstractSocket::IPv4Protocol
			|| address.toString() != raw) {
			return false;
		}
	}
	const auto normalizeDomains = [](QStringList &domains) {
		for (auto &domain : domains) {
			const auto ace = QUrl::toAce(domain);
			if (ace.isEmpty() || ace.size() > 253 || !ace.contains('.')
				|| ranges::any_of(ace, [](char ch) {
					return !((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')
						|| ch == '.' || ch == '-');
				})) {
				return false;
			}
			domain = QString::fromLatin1(ace);
		}
		return true;
	};
	if (!normalizeDomains(options.cloudflareDomains)
		|| !normalizeDomains(options.workerDomains)) {
		return false;
	}
	const auto folder = ProxyDirectory();
	auto config = ReadConfiguration(folder);
	if (!config) {
		return false;
	}
	config->insert(u"dc_ip"_q, QJsonArray::fromStringList(options.dcOverrides));
	config->insert(u"cfproxy"_q, options.cloudflareFallback);
	config->insert(u"h2"_q, options.http2);
	config->insert(u"no_secure"_q, !options.secure);
	config->insert(u"cfproxy_user_domain_enabled"_q, !options.cloudflareDomains.isEmpty());
	config->insert(u"cfproxy_user_domain"_q, QJsonArray::fromStringList(options.cloudflareDomains));
	config->insert(u"cfproxy_worker_enabled"_q, !options.workerDomains.isEmpty());
	config->insert(u"cfproxy_worker_domain"_q, QJsonArray::fromStringList(options.workerDomains));
	if (!WriteConfiguration(folder, std::move(*config))) {
		return false;
	}
	auto &settings = Core::App().settings();
	settings.writePref<int>(kProxyPortPref, std::clamp(options.port, 1, 65535));
	settings.writePref<int>(kProxyBufferPref, std::clamp(options.bufferKiB, 4, 4096));
	settings.writePref<int>(kProxyPoolPref, std::clamp(options.poolSize, 0, 32));
	settings.writePref<bool>(kProxyVerbosePref, options.verbose);
	Core::App().saveSettingsDelayed();
	return true;
}

} // namespace Platform
