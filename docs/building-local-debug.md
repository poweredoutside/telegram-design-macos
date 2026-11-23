# Локальная Debug-сборка на этом компьютере

Проект открыт как нативная Windows-копия. Подготовленное окружение находится в `C:\TelegramBuild`, зависимости — в `C:\Users\power\Desktop\Libraries\win64`. Используется Qt 6.11.2 и портативный MSVC 14.44 с Windows SDK 10.0.26100.0.

Из PowerShell:

```powershell
cmd.exe /d /c C:\TelegramBuild\build-client.cmd
```

Скрипт сначала конфигурирует `out` через Ninja Multi-Config, затем собирает цель `Telegram` в конфигурации Debug. Число параллельных компиляций в скрипте подбирается по доступной памяти. Объекты от предыдущих запусков используются повторно.

Результат: `C:\Users\power\Desktop\telegram-design-macos-main\out\Debug\Telegram.exe`. Конфигурация записывается в `C:\TelegramBuild\client-configure.log`, компиляция — в `C:\TelegramBuild\client-build.log`. Для запущенного через `run-client-build.cmd` фонового процесса итоговый код записывается в `client-build.exitcode`; ноль означает успешное завершение скрипта. Наличие EXE само по себе не доказывает успех последней сборки.

Для проверки с отдельным локальным профилем:

```powershell
New-Item -ItemType Directory -Force C:\TelegramBuild\design-check
& C:\Users\power\Desktop\telegram-design-macos-main\out\Debug\Telegram.exe -many -workdir C:\TelegramBuild\design-check
```

Для повторной проверки используется тот же каталог профиля. Он хранит данные входа в тестируемый клиент. Перед пересборкой запущенный клиент следует закрыть самостоятельно, чтобы освободить EXE и PDB.

Конфигурация предназначена для локальной проверки: `TDESKTOP_API_TEST=ON`, автоматическое обновление отключено. Применяются ограниченные тестовые API-реквизиты из штатной конфигурации проекта. Для распространяемой сборки нужны собственные реквизиты, описанные в [api_credentials.md](api_credentials.md).

Исходная папка получена из архива; восстановленные подмодули и собранные зависимости не заменяют корневую историю Git. Результаты сверки с донором и оставшиеся визуальные проверки описаны в [macos-donor-design.md](macos-donor-design.md).
