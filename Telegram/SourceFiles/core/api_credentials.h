#pragma once

#include "base/basic_types.h"

class QObject;

namespace Core::ApiCredentials {

void Prepare(QObject *context, Fn<void(bool)> done);
[[nodiscard]] int Id();
[[nodiscard]] const char *Hash();
[[nodiscard]] int CrashReportId();

} // namespace Core::ApiCredentials
