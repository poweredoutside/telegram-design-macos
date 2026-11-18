#pragma once

namespace Platform {

class BundledTgWsProxy final {
public:
	BundledTgWsProxy() = default;
	~BundledTgWsProxy();
	BundledTgWsProxy(const BundledTgWsProxy &) = delete;
	BundledTgWsProxy &operator=(const BundledTgWsProxy &) = delete;

	void start();

private:
	void *_job = nullptr;

};

} // namespace Platform
