# Telega Desktop for Windows

Неофициальная модификация Telegram Desktop для Windows с оформлением в стиле нативного Telegram для macOS.
Проект не связан с Telegram или Apple и не является официальным клиентом Telegram для macOS.

## Скачать установщик

[Скачать предыдущую Preview 1 для Windows x64](https://github.com/poweredoutside/telegram-design-macos/releases/download/v7.4.4-macos-design-preview.1/Telegram-macOS-Design-Setup-x64.exe)

[Страница релизов и контрольные суммы](https://github.com/poweredoutside/telegram-design-macos/releases)

Текущая версия — **7.4.4 macOS Design Preview 1**: тестовая **Debug-сборка без цифровой подписи**.
Windows может показать предупреждение о неизвестном издателе. Это не подписанная Release-сборка.
Установщик предназначен для Windows 10 версии 1809 и новее, x64; работа на чистой системе отдельно не проверялась.

Установите приложение в отдельную папку, предложенную установщиком, и войдите в свой аккаунт.
Ярлыки новой версии называются **Telega Desktop**. Установщик не содержит аккаунтов разработчика.

Данные аккаунта хранятся в `TelegramForcePortable` рядом с установленным `Telegram.exe`.
Удаление приложения сохраняет эту папку. Для полного удаления сначала выйдите из аккаунта,
затем удалите оставшуюся папку с данными вручную. Обновления этой сборки устанавливаются вручную.

## Что изменено

- Нижняя навигация: контакты, звонки, чаты и настройки.
- Оформление списка чатов, выбранных строк, панели окна и настроек в стиле macOS.
- Светлая и тёмная темы, иконки из открытого исходного кода Telegram для macOS.
- Настраиваемый эффект Liquid Glass для нижней навигации: прозрачность, размытие и преломление.
- Масштаб интерфейса: **Настройки → Оформление → Масштаб интерфейса**.
- Параметры Liquid Glass: **Настройки → Оформление → Liquid Glass**.

Это адаптация интерфейса Telegram Desktop для Windows; полное визуальное и функциональное совпадение с нативным macOS-клиентом не заявляется.

В текущих исходниках продолжается перенос обеих тем непосредственно из закреплённой версии
Telegram для macOS. Эти изменения ещё не собраны и не входят в установщик Preview 1 выше.
Что перенесено, откуда взяты параметры и что остаётся проверить: [статус переноса](docs/macos-donor-design.md).

## Получить исходники

```powershell
git clone --recurse-submodules https://github.com/poweredoutside/telegram-design-macos.git tdesktop
cd tdesktop
```

Остальные подмодули закреплены на конкретных версиях публичных upstream-репозиториев.
`Telegram/lib_ui` включён непосредственно в этот репозиторий, потому что содержит изменения заголовка окна для этого оформления.
Его основа — `desktop-app/lib_ui`, коммит `caaa3f1eb20bab7ef838c6aea35db2397e8f3f07`.
Основа основного дерева — `telegramdesktop/tdesktop`, коммит `bf4f714036f21c750d535dab199c811b3f9bfdd4` (7.4.4).

## Сборка

Полная подготовка зависимостей описана в [инструкции для Windows](docs/building-win.md).
В команде клонирования из той инструкции используйте адрес этого репозитория.
Для самостоятельной сборки получите собственные `api_id` и `api_hash` на [my.telegram.org](https://my.telegram.org).
Не публикуйте свои API-реквизиты или папки аккаунта.

Опубликованная тестовая сборка проверена с Visual Studio Build Tools 2022,
MSVC toolset 14.44, Python 3.10 и Qt 6.11.2. Upstream-инструкция также описывает более новые версии Visual Studio.
После подготовки библиотек и настройки `out` из соответствующей x64 Native Tools Command Prompt:

```powershell
cmake --build out --config Debug --target Telegram --parallel 3
```

Для упаковки уже собранного приложения используйте Inno Setup 6.7.4:

```powershell
& 'C:\path\to\ISCC.exe' 'Telegram\build\setup_macos_design.iss'
```

Результат: `out/setup/Telega-Desktop-Setup-x64.exe`.
[Описание установщика](Telegram/build/setup_macos_design.md).
Библиотеки сборки и инструменты нужно подготовить отдельно; их бинарные файлы не включены в репозиторий.
Исходные upstream-workflows сохранены в `docs/upstream-workflows` и автоматически не запускаются.

## Проверка опубликованной Preview 1

Debug-сборка успешно собрана, интерфейс проверен в вошедшей тестовой копии.
Для установщика проверены установка, повторная установка, ярлыки, совпадение SHA-256 файлов
и удаление с сохранением данных аккаунта. Приложение из установленной копии на чистой Windows отдельно не проверялось.

## Лицензии и авторство

Telegram Desktop распространяется по GPLv3 с исключением для OpenSSL: [LICENSE](LICENSE), [LEGAL](LEGAL).
Авторские права исходных авторов сохранены. Лицензии зависимостей находятся в их каталогах.
Источники иконок Telegram для macOS: [SOURCES.md](Telegram/Resources/icons/macos/SOURCES.md).
Оригинальное описание upstream: [docs/telegram-desktop-upstream.md](docs/telegram-desktop-upstream.md).

## English

Unofficial Windows customization of Telegram Desktop with a macOS-inspired interface,
bottom navigation, configurable Liquid Glass effects and interface scaling.
The current installer is an **unsigned Debug preview**, not a production Release build.
Use the Releases page to download it. Clone with `--recurse-submodules` to obtain the source dependencies.
The installer uses a separate application folder and preserves its account-data folder on uninstall.
