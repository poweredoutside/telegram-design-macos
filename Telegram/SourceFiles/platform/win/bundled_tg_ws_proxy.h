#pragma once

#include "mtproto/mtproto_proxy_data.h"

#include <QtCore/QStringList>

namespace Platform {

class BundledTgWsProxy final {
public:
	struct Options {
		int port = 1443;
		int bufferKiB = 256;
		int poolSize = 4;
		bool verbose = false;
		bool cloudflareFallback = true;
		bool http2 = true;
		bool secure = true;
		QStringList dcOverrides;
		QStringList cloudflareDomains;
		QStringList workerDomains;
	};

	BundledTgWsProxy() = default;
	~BundledTgWsProxy();
	BundledTgWsProxy(const BundledTgWsProxy &) = delete;
	BundledTgWsProxy &operator=(const BundledTgWsProxy &) = delete;

	[[nodiscard]] MTP::ProxyData start();
	void stop();
	[[nodiscard]] bool running() const;
	[[nodiscard]] static bool Available();
	[[nodiscard]] static bool StartAutomatically();
	static void SetStartAutomatically(bool enabled);
	[[nodiscard]] static Options Configuration();
	[[nodiscard]] static bool SetConfiguration(Options options);

private:
	void *_job = nullptr;

};

} // namespace Platform
