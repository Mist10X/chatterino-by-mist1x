# Авторы и лицензии

**Chatterino by Mist1X** — неофициальная сборка Twitch-чата. Она построена на коде проектов, перечисленных ниже, и не связана ни с их авторами, ни с Twitch. Все эти проекты распространяются по лицензии MIT: её текст лежит в файле `LICENSE`, и уведомления об авторских правах этих проектов сохранены в исходном коде.

## На чём основана сборка

| Проект | Авторы | Что взято |
|---|---|---|
| [Chatterino 2](https://github.com/Chatterino/chatterino2) | Chatterino contributors | основа программы |
| [Chatterino7](https://github.com/SevenTV/chatterino7) | 7TV и участники проекта | поддержка 7TV: смайлики, значки, краски, личные смайлики |
| [Leafyrino](https://github.com/Leafyzito/leafyrino) | Leafyzito и участники проекта | сборка функций из форков ниже и собственные функции Leafyrino |
| [Technorino](https://github.com/2547techno/technorino) | 2547techno и участники проекта | функции Technorino, вошедшие в Leafyrino |
| [Chatterino7 (форк itzAlex)](https://github.com/itzAlex/chatterino7) | itzAlex | значки homies |
| [Moltorino7](https://github.com/EEVA-inc/Moltorino7) | EEVA-inc и участники проекта | закрепы, опросы, прогнозы, баллы канала и другие функции Moltorino |

## Что добавлено в этой сборке

Автор изменений — Mist1X (2026). В том числе: русский интерфейс, настройки по темам, окно «С чего начать» и перенос настроек из другого Chatterino, автообновление с GitHub, собственное оформление (тема Mist-Dark, шрифт Mist Montserrat), отсчёт и очередь медленного режима, мут и бан в строке ввода, чтение чата при бане, смайлики в строке ввода, история сообщений из публичных логов в карточке пользователя, переработанная карточка, вставка смайлика правой кнопкой и другие изменения. Полный список — в истории коммитов репозитория.

## Сторонние компоненты

В программу входят библиотеки и ресурсы со своими лицензиями. Их тексты лежат в папке `LICENSES/third-party` рядом с программой и в разделе «О программе».

- [Qt](https://www.qt.io) — LGPL-3.0. Подключается как отдельные DLL; исходный код Qt доступен на qt.io, библиотеки можно заменить своими.
- [OpenSSL](https://www.openssl.org) — Apache-2.0.
- Шрифт [Montserrat](https://github.com/google/fonts/tree/main/ofl/montserrat) — SIL Open Font License 1.1. В сборке он называется «Mist Montserrat»; переименование разрешено лицензией.
- Boost, libcommuni, rapidjson, pajlada/settings, pajlada/signals, pajlada/serialize, magic_enum, semver, miniaudio, crashpad, Fluent UI System Icons, expected-lite, certify, date, hunspell, qtkeychain, kimageformats, fmt, lrucache, Lua, Unicode data, emoji-data.
- Звуки Chatterino — по лицензии из `LICENSES/LicenseRef-fourtf-chatterino-sound-license.txt`.

Twitch, 7TV, BetterTTV и FrankerFaceZ — названия соответствующих сервисов; сборка использует их открытые API и не является их продуктом.
