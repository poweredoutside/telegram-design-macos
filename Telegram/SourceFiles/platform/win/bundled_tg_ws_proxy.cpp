#include "platform/win/bundled_tg_ws_proxy.h"

#include "logs.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

#include <windows.h>

namespace Platform {
namespace {

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

[[nodiscard]] bool EnsureConfiguration(const QDir &folder) {
	const auto dataDir = u"TgWsProxy_data"_q;
	if (!folder.mkpath(dataDir)) {
		return false;
	}
	const auto path = folder.filePath(dataDir + u"/config.json"_q);
	auto config = QJsonObject();
	auto file = QFile(path);
	if (file.exists()) {
		if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024) {
			return false;
		}
		const auto document = QJsonDocument::fromJson(file.readAll());
		if (!document.isObject()) {
			return false;
		}
		config = document.object();
		file.close();
	}
	config.insert(u"host"_q, u"127.0.0.1"_q);
	config.insert(u"check_updates"_q, false);
	config.insert(u"autostart"_q, false);
	return SaveFile(path, QJsonDocument(config).toJson(QJsonDocument::Compact));
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
	if (_job) {
		CloseHandle(_job);
	}
}

void BundledTgWsProxy::start() {
	if (_job || !QDir(cExeDir() + u"tgwsproxy.auto"_q).exists()) {
		return;
	}
	if (const auto external = OpenMutexW(
			SYNCHRONIZE,
			FALSE,
			L"Local\\TgWsProxy_SingleInstance")) {
		CloseHandle(external);
		return;
	}
	const auto folder = QDir(cWorkingDir() + u"tgwsproxy"_q);
	const auto path = folder.filePath(u"TgWsProxy.exe"_q);
	if (EnsureConfiguration(folder) && EnsureBinary(path)) {
		_job = StartInJob(path, folder.absolutePath());
	}
	if (!_job) {
		LOG(("TgWsProxy: could not start the bundled proxy."));
	}
}

} // namespace Platform
