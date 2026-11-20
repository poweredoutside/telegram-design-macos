/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_app_user_model_id.h"
#include "platform/win/windows_telega_resources.h"

#include "platform/win/windows_dlls.h"
#include "platform/win/windows_toast_activator.h"
#include "platform/win/tray_win.h"
#include "base/platform/win/base_windows_shlobj_h.h"
#include "base/platform/win/base_windows_winrt.h"
#include "core/launcher.h"

#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <propvarutil.h>
#include <propkey.h>

namespace Platform {
namespace AppUserModelId {
namespace {

constexpr auto kMaxFileLen = MAX_PATH * 2;
const auto kUninstallKey = u"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\"
	u"CurrentVersion\\Uninstall\\{AE8B9692-D9D9-43B0-B1C9-8923E9C75E2B}_is1"_q;

const PROPERTYKEY pkey_AppUserModel_ID = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 5 };
const PROPERTYKEY pkey_AppUserModel_StartPinOption = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 12 };
const PROPERTYKEY pkey_AppUserModel_ToastActivator = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 26 };

#ifdef OS_WIN_STORE
const WCHAR AppUserModelIdBase[] = L"PoweredOutside.TelegaDesktop.Store";
#else // OS_WIN_STORE
const WCHAR AppUserModelIdBase[] = L"PoweredOutside.TelegaDesktop";
#endif // OS_WIN_STORE

[[nodiscard]] QString PinnedIconsPath() {
	WCHAR wstrPath[kMaxFileLen] = {};
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, kMaxFileLen)) {
		auto appData = QDir(QString::fromStdWString(std::wstring(wstrPath)));
		return appData.absolutePath()
			+ u"/Microsoft/Internet Explorer/Quick Launch/User Pinned/TaskBar/"_q;
	}
	return QString();
}

} // namespace

const std::wstring &MyExecutablePath() {
	static const auto Path = [&] {
		auto result = std::wstring(kMaxFileLen, 0);
		const auto length = GetModuleFileName(
			GetModuleHandle(nullptr),
			result.data(),
			kMaxFileLen);
		if (!length || length == kMaxFileLen) {
			result.clear();
		} else {
			result.resize(length + 1);
		}
		return result;
	}();
	return Path;
}

UniqueFileId MyExecutablePathId() {
	return GetUniqueFileId(MyExecutablePath().c_str());
}

UniqueFileId GetUniqueFileId(LPCWSTR path) {
	auto info = BY_HANDLE_FILE_INFORMATION{};
	const auto file = CreateFile(
		path,
		0,
		0,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return {};
	}
	const auto result = GetFileInformationByHandle(file, &info);
	CloseHandle(file);
	if (!result) {
		return {};
	}
	return {
		.part1 = info.dwVolumeSerialNumber,
		.part2 = ((std::uint64_t(info.nFileIndexLow) << 32)
			| std::uint64_t(info.nFileIndexHigh)),
	};
}

void CheckPinned() {
	if (!SUCCEEDED(CoInitialize(0))) {
		return;
	}
	const auto coGuard = gsl::finally([] {
		CoUninitialize();
	});

	const auto path = PinnedIconsPath();
	const auto native = QDir::toNativeSeparators(path).toStdWString();

	const auto srcid = MyExecutablePathId();
	if (!srcid) {
		return;
	}

	LOG(("Checking..."));
	WIN32_FIND_DATA findData;
	HANDLE findHandle = FindFirstFileEx(
		(native + L"*").c_str(),
		FindExInfoStandard,
		&findData,
		FindExSearchNameMatch,
		0,
		0);
	if (findHandle == INVALID_HANDLE_VALUE) {
		LOG(("Init Error: could not find files in pinned folder"));
		return;
	}
	do {
		std::wstring fname = native + findData.cFileName;
		LOG(("Checking %1").arg(QString::fromStdWString(fname)));
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			continue;
		} else {
			DWORD attributes = GetFileAttributes(fname.c_str());
			if (attributes >= 0xFFFFFFF) {
				continue; // file does not exist
			}

			auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(
				CLSID_ShellLink);
			if (!shellLink) {
				continue;
			}

			auto persistFile = shellLink.try_as<IPersistFile>();
			if (!persistFile) {
				continue;
			}

			auto hr = persistFile->Load(fname.c_str(), STGM_READWRITE);
			if (!SUCCEEDED(hr)) continue;

			WCHAR dst[MAX_PATH] = { 0 };
			hr = shellLink->GetPath(dst, MAX_PATH, nullptr, 0);
			if (!SUCCEEDED(hr)) continue;

			if (GetUniqueFileId(dst) == srcid) {
				auto propertyStore = shellLink.try_as<IPropertyStore>();
				if (!propertyStore) {
					return;
				}

				PROPVARIANT appIdPropVar;
				hr = propertyStore->GetValue(Key(), &appIdPropVar);
				if (!SUCCEEDED(hr)) return;
				LOG(("Reading..."));
				WCHAR already[MAX_PATH];
				hr = PropVariantToString(appIdPropVar, already, MAX_PATH);
				if (SUCCEEDED(hr)) {
					if (Id() == already) {
						LOG(("Already!"));
						PropVariantClear(&appIdPropVar);
						return;
					}
				}
				if (appIdPropVar.vt != VT_EMPTY) {
					PropVariantClear(&appIdPropVar);
					return;
				}
				PropVariantClear(&appIdPropVar);

				hr = InitPropVariantFromString(Id().c_str(), &appIdPropVar);
				if (!SUCCEEDED(hr)) return;

				hr = propertyStore->SetValue(Key(), appIdPropVar);
				PropVariantClear(&appIdPropVar);
				if (!SUCCEEDED(hr)) return;

				hr = propertyStore->Commit();
				if (!SUCCEEDED(hr)) return;

				if (persistFile->IsDirty() == S_OK) {
					persistFile->Save(fname.c_str(), TRUE);
				}
				return;
			}
		}
	} while (FindNextFile(findHandle, &findData));
	DWORD errorCode = GetLastError();
	if (errorCode && errorCode != ERROR_NO_MORE_FILES) {
		LOG(("Init Error: could not find some files in pinned folder"));
		return;
	}
	FindClose(findHandle);
}

QString systemShortcutPath() {
	WCHAR wstrPath[kMaxFileLen] = {};
	if (GetEnvironmentVariable(L"APPDATA", wstrPath, kMaxFileLen)) {
		auto appData = QDir(QString::fromStdWString(std::wstring(wstrPath)));
		const auto path = appData.absolutePath();
		return path + u"/Microsoft/Windows/Start Menu/Programs/"_q;
	}
	return QString();
}

bool IsInstalled() {
	static const auto Installed = [] {
		const auto dir = QSettings(kUninstallKey, QSettings::Registry64Format)
			.value(u"InstallLocation"_q).toString();
		const auto myid = MyExecutablePathId();
		return !dir.isEmpty() && myid && (myid == GetUniqueFileId(
			QDir(dir).filePath(u"Telegram.exe"_q).toStdWString().c_str()));
	}();
	return Installed;
}

QString fallbackShortcutName() {
	return IsInstalled()
		? u"Telega Desktop.lnk"_q
		: u"Telega Desktop (Portable).lnk"_q;
}

void SetWindowRelaunchIcon(HWND window) {
	const auto icon = Tray::ApplicationShortcutIconPath();
	const auto executable = MyExecutablePath().c_str();
	auto store = winrt::com_ptr<IPropertyStore>();
	if (icon.isEmpty() || !*executable
		|| FAILED(SHGetPropertyStoreForWindow(
			window,
			__uuidof(IPropertyStore),
			store.put_void()))) {
		return;
	}
	const auto command = L"\"" + std::wstring(executable) + L"\"";
	const auto name = L"@" + std::wstring(executable) + L",-"
		+ std::to_wstring(IDS_TELEGA_DESKTOP);
	const auto resource = icon.toStdWString() + L",0";
	const auto set = [&](const PROPERTYKEY &key, const std::wstring &value) {
		auto property = PROPVARIANT();
		if (FAILED(InitPropVariantFromString(value.c_str(), &property))) {
			return false;
		}
		const auto result = SUCCEEDED(store->SetValue(key, property));
		PropVariantClear(&property);
		return result;
	};
	if (set(PKEY_AppUserModel_RelaunchCommand, command)
		&& set(PKEY_AppUserModel_RelaunchDisplayNameResource, name)
		&& set(PKEY_AppUserModel_RelaunchIconResource, resource)) {
		set(Key(), Id());
	}
}

void RefreshShortcutIcons() {
	const auto myid = MyExecutablePathId();
	if (!myid || !SUCCEEDED(CoInitialize(nullptr))) {
		return;
	}
	const auto coGuard = gsl::finally([] { CoUninitialize(); });
	const auto iconPath = Tray::ApplicationShortcutIconPath().toStdWString();
	if (iconPath.empty()) {
		return;
	}
	const auto update = [&](const QString &path) {
		if (!QFile::exists(path)) {
			return;
		}
		const auto link = base::WinRT::TryCreateInstance<IShellLink>(CLSID_ShellLink);
		if (!link) {
			return;
		}
		const auto file = link.try_as<IPersistFile>();
		const auto native = QDir::toNativeSeparators(path).toStdWString();
		auto target = std::array<WCHAR, kMaxFileLen>();
		if (!file
			|| !SUCCEEDED(file->Load(native.c_str(), STGM_READWRITE))
			|| !SUCCEEDED(link->GetPath(target.data(), kMaxFileLen, nullptr, 0))
			|| GetUniqueFileId(target.data()) != myid) {
			return;
		}
		auto currentIcon = std::array<WCHAR, kMaxFileLen>();
		auto index = int();
		if (SUCCEEDED(link->GetIconLocation(currentIcon.data(), kMaxFileLen, &index))
			&& !index && iconPath == currentIcon.data()) {
			return;
		}
		if (SUCCEEDED(link->SetIconLocation(iconPath.c_str(), 0))
			&& SUCCEEDED(file->Save(native.c_str(), TRUE))) {
			SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, native.c_str(), nullptr);
		}
	};
	if (const auto programs = systemShortcutPath(); !programs.isEmpty()) {
		update(programs + fallbackShortcutName());
		if (IsInstalled()) {
			const auto group = QSettings(kUninstallKey, QSettings::Registry64Format)
				.value(u"Inno Setup: Icon Group"_q, u"Telega Desktop"_q).toString();
			update(QDir(programs + group).filePath(u"Telega Desktop.lnk"_q));
		}
	}
	if (IsInstalled()) {
		const auto desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
		if (!desktop.isEmpty()) {
			update(QDir(desktop).filePath(u"Telega Desktop.lnk"_q));
		}
	}
	if (const auto pinned = PinnedIconsPath(); !pinned.isEmpty()) {
		for (const auto &link : QDir(pinned).entryInfoList({ u"*.lnk"_q }, QDir::Files)) {
			update(link.absoluteFilePath());
		}
	}
}

void CleanupShortcut() {
	const auto myid = MyExecutablePathId();
	if (!myid) {
		return;
	}

	QString path = systemShortcutPath() + fallbackShortcutName();
	std::wstring p = QDir::toNativeSeparators(path).toStdWString();

	DWORD attributes = GetFileAttributes(p.c_str());
	if (attributes >= 0xFFFFFFF) return; // file does not exist

	auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(
		CLSID_ShellLink);
	if (!shellLink) {
		return;
	}

	auto persistFile = shellLink.try_as<IPersistFile>();
	if (!persistFile) {
		return;
	}

	auto hr = persistFile->Load(p.c_str(), STGM_READWRITE);
	if (!SUCCEEDED(hr)) return;

	WCHAR szGotPath[MAX_PATH];
	hr = shellLink->GetPath(szGotPath, MAX_PATH, nullptr, 0);
	if (!SUCCEEDED(hr)) return;

	if (GetUniqueFileId(szGotPath) == myid) {
		QFile().remove(path);
	}
}

bool validateShortcutAt(const QString &path) {
	const auto native = QDir::toNativeSeparators(path).toStdWString();

	DWORD attributes = GetFileAttributes(native.c_str());
	if (attributes >= 0xFFFFFFF) {
		return false; // file does not exist
	}

	auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(
		CLSID_ShellLink);
	if (!shellLink) {
		return false;
	}

	auto persistFile = shellLink.try_as<IPersistFile>();
	if (!persistFile) {
		return false;
	}

	auto hr = persistFile->Load(native.c_str(), STGM_READWRITE);
	if (!SUCCEEDED(hr)) return false;

	WCHAR szGotPath[kMaxFileLen] = { 0 };
	hr = shellLink->GetPath(szGotPath, kMaxFileLen, nullptr, 0);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	if (GetUniqueFileId(szGotPath) != MyExecutablePathId()) {
		return false;
	}

	auto propertyStore = shellLink.try_as<IPropertyStore>();
	if (!propertyStore) {
		return false;
	}

	PROPVARIANT appIdPropVar;
	PROPVARIANT toastActivatorPropVar;
	hr = propertyStore->GetValue(Key(), &appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->GetValue(
		pkey_AppUserModel_ToastActivator,
		&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) return false;

	WCHAR already[MAX_PATH];
	hr = PropVariantToString(appIdPropVar, already, MAX_PATH);
	const auto good1 = SUCCEEDED(hr) && (Id() == already);
	const auto bad1 = !good1 && (appIdPropVar.vt != VT_EMPTY);
	PropVariantClear(&appIdPropVar);

	auto clsid = CLSID();
	hr = PropVariantToCLSID(toastActivatorPropVar, &clsid);
	const auto good2 = SUCCEEDED(hr) && (clsid == __uuidof(ToastActivator));
	const auto bad2 = !good2 && (toastActivatorPropVar.vt != VT_EMPTY);
	PropVariantClear(&toastActivatorPropVar);
	if (good1 && good2) {
		LOG(("App Info: Shortcut validated at \"%1\"").arg(path));
		return true;
	} else if (bad1 || bad2) {
		return false;
	}

	hr = InitPropVariantFromString(Id().c_str(), &appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->SetValue(Key(), appIdPropVar);
	PropVariantClear(&appIdPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = InitPropVariantFromCLSID(
		__uuidof(ToastActivator),
		&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->SetValue(
		pkey_AppUserModel_ToastActivator,
		toastActivatorPropVar);
	PropVariantClear(&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) return false;

	hr = propertyStore->Commit();
	if (!SUCCEEDED(hr)) return false;

	if (persistFile->IsDirty() == S_OK) {
		hr = persistFile->Save(native.c_str(), TRUE);
		if (!SUCCEEDED(hr)) return false;
	}

	LOG(("App Info: Shortcut set and validated at \"%1\"").arg(path));
	return true;
}

bool ValidateShortcut() {
	QString path = systemShortcutPath();
	if (path.isEmpty() || cExeName().isEmpty()) {
		return false;
	}

	if (IsInstalled()
		&& validateShortcutAt(path + u"Telega Desktop/Telega Desktop.lnk"_q)) {
		return true;
	}
	path += fallbackShortcutName();
	if (validateShortcutAt(path)) {
		return true;
	}

	auto shellLink = base::WinRT::TryCreateInstance<IShellLink>(
		CLSID_ShellLink);
	if (!shellLink) {
		return false;
	}

	auto hr = shellLink->SetPath(MyExecutablePath().c_str());
	if (!SUCCEEDED(hr)) {
		return false;
	}
	if (const auto icon = Tray::ApplicationShortcutIconPath(); !icon.isEmpty()) {
		shellLink->SetIconLocation(icon.toStdWString().c_str(), 0);
	}

	hr = shellLink->SetArguments(L"");
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = shellLink->SetWorkingDirectory(
		QDir::toNativeSeparators(
			QDir(cWorkingDir()).absolutePath()).toStdWString().c_str());
	if (!SUCCEEDED(hr)) {
		return false;
	}

	auto propertyStore = shellLink.try_as<IPropertyStore>();
	if (!propertyStore) {
		return false;
	}

	PROPVARIANT appIdPropVar;
	hr = InitPropVariantFromString(Id().c_str(), &appIdPropVar);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = propertyStore->SetValue(Key(), appIdPropVar);
	PropVariantClear(&appIdPropVar);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	PROPVARIANT startPinPropVar;
	hr = InitPropVariantFromUInt32(
		APPUSERMODEL_STARTPINOPTION_NOPINONINSTALL,
		&startPinPropVar);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = propertyStore->SetValue(
		pkey_AppUserModel_StartPinOption,
		startPinPropVar);
	PropVariantClear(&startPinPropVar);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	PROPVARIANT toastActivatorPropVar{};
	hr = InitPropVariantFromCLSID(
		__uuidof(ToastActivator),
		&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = propertyStore->SetValue(
		pkey_AppUserModel_ToastActivator,
		toastActivatorPropVar);
	PropVariantClear(&toastActivatorPropVar);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = propertyStore->Commit();
	if (!SUCCEEDED(hr)) {
		return false;
	}

	auto persistFile = shellLink.try_as<IPersistFile>();
	if (!persistFile) {
		return false;
	}

	hr = persistFile->Save(
		QDir::toNativeSeparators(path).toStdWString().c_str(),
		TRUE);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	LOG(("App Info: Shortcut created and validated at \"%1\"").arg(path));
	return true;
}

const std::wstring &Id() {
	static const auto BaseId = std::wstring(AppUserModelIdBase);
#ifdef OS_WIN_STORE
	return BaseId;
#else // OS_WIN_STORE
	if (IsInstalled()) {
		return BaseId;
	}
	static const auto PortableId = [] {
		const auto h = Core::Launcher::Instance().instanceHash();
		return BaseId + L'.' + std::wstring(h.begin(), h.end());
	}();
	return PortableId;
#endif // OS_WIN_STORE
}

const PROPERTYKEY &Key() {
	return pkey_AppUserModel_ID;
}

} // namespace AppUserModelId
} // namespace Platform
