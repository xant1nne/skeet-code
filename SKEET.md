# SKEET Code

Терминальный агент для программирования на C в духе Claude Code.
Работает с локальными LLM-моделями через **Ollama** и **llama.cpp** (с автоподбором параметров под железо).

## Принципы архитектуры
1. **Один исполняемый бинарник:** `skeet`.
2. **Минимум внешних зависимостей:** только `libcurl` и `cJSON` (в `third_party/`). Никаких тяжелых фреймворков.
3. **Один поток + poll:** весь I/O (терминал, HTTP, субпроцессы) строится на неблокирующем/polling подходе без гонок потоков.
4. **Арена памяти на ход (`Arena`):** scratch-память аллоцируется на время одного шага агента и очищается через `arena_reset` без фрагментации кучи.
5. **Строки `Str`:** легковесные срезы `(ptr, len)` без лишних `malloc`/`strlen`.

## Структура проекта
```
skeet-code/
  Makefile                  # Сборка и тесты с ASan/UBSan
  SKEET.md                  # Документация проекта для агента и разработчика
  src/
    main.c                  # Точка входа, CLI аргументы
    arena.c/h               # Bump-аллокатор (арена на ход)
    str.c/h                 # Str (ptr, len) и StrBuf (динамический буфер)
    config.c/h              # Конфигурация (~/.config/skeet/config.json)
    log.c/h                 # Логирование в файл
    agent.c/h               # REPL / главный цикл агента
    backend.h               # Абстракция бэкенда (list_models, start, chat_stream, stop)
    backend_ollama.c        # Реализация бэкенда для Ollama (/api/tags, /v1/chat/completions)
    backend_llama.c         # Бэкенд llama.cpp (M3)
    http.c/h                # libcurl + curl_multi_poll + SseParser
    json.c/h                # Помощники cJSON
    tools/                  # Набор инструментов агента (M2)
    tune/                   # Детект железа и автонастройка флагов llama.cpp (M4)
    tui/                    # Терминальный интерфейс (M5)
  tests/
    mock_server.c           # Локальный мок-сервер SSE/Ollama
    test_arena.c
    test_str.c
    test_sse.c
    test_json.c
  third_party/
    cJSON.c/h
```

## Статус этапов
- [x] **M0. Каркас:** Makefile, сборка с `-Wall -Wextra -pedantic -fsanitize=address,undefined`, арена, строки, логи, чтение/запись конфига.
- [x] **M1. Чат со стримингом (Ollama):** интерфейс Backend, список моделей через `/api/tags`, SSE стриминг токенов, отмена по Ctrl+C без падений, юнит-тесты и mock-сервер.
- [x] **M2. Tool loop:** инструменты `read_file`, `glob`, `grep`, `edit_file`, `write_file`, `bash`, парсинг tool calls, подтверждения, шина событий.
- [x] **M3. Бэкенд llama.cpp:** спавн `llama-server`, health-чек, поддержка локальных GGUF, graceful shutdown, переключение `/backend`.
- [x] **M4. Автоподбор под железо:** детект RAM/VRAM/ядер (Linux/macOS/Windows, NVML/nvidia-smi/Metal/sysfs), парсер заголовка GGUF без чтения тензоров, калькулятор параметров (`-ngl`, `-c`, `-t`, `-fa`, `-ctk/-ctv`), кэширование в `tune.json`, команда `/tune` и авто-подбор при запуске llama-server.
- [x] **M5. TUI и динамические библиотеки (.so / .dll / Rust):** полноэкранный TUI (альтернативный экран `term.c/h`, скролл `view.c/h`, подсветка синтаксиса и diff, строковый редактор `input.c/h`, история команд, Tab-комплит). Архитектура «1 главный бинарник + динамические плагины инструментов» (`plugin_loader.c/h`, `plugin_abi.h`, Rust cdylib `plugins/rust_codex_tools/` с инструментами из открытых аналогов Codex: `codex_fast_grep`, `codex_outline_symbols`, `codex_apply_patch`).
- [x] **M6. Качество агента и контекст:** авто-инъекция `SKEET.md` в `system` prompt, точный счетчик токенов `context_estimate_tokens`, автоматический и ручной Context Compactor (`/compact`), управление сессиями (`/session save/load/list`), переключение моделей на лету (`/model [name]`).
- [x] **M7. Плагины на LuaJIT:** встраивание Lua-рантайма (`lua_host.c/h`), `skeet.*` API (`register_tool`, `register_command`, `log`), автозагрузка скриптов из `plugins/*.lua` и `.skeet/plugins/`, изоляция и выгрузка по владельцу.
- [x] **M8. Релиз v1.0:** статическая сборка (musl-gcc / lipo universal), CI/CD матрица (GitHub Actions: Linux/macOS/Windows), скрипт быстрой установки `scripts/install.sh`, man-страница `docs/skeet.1`, `make release / install / uninstall`, версия `1.0.0`.

## Сборка и тестирование
- Сборка бинарника: `make` (по умолчанию с AddressSanitizer и UndefinedBehaviorSanitizer)
- Релизная сборка (платформо-нативная): `make release`
- Релизная сборка без ASan: `make RELEASE=1`
- Запуск тестов: `make test`
- Установка в систему: `sudo make install`  (→ `/usr/local/bin/skeet`, man page)
- Удаление: `sudo make uninstall`
- Сборка плагинов: `make plugins`
- Сборка мок-сервера: `make mock_server`

## Быстрая установка
```bash
curl -fsSL https://raw.githubusercontent.com/your-org/skeet-code/main/scripts/install.sh | bash
```
