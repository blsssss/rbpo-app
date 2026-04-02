# РБПО — Tray Application

Графическое приложение для Windows с иконкой в области уведомлений (трей), реализованное на **C++ / Win32 API** с системой сборки **CMake**.

## Структура проекта

```
rbpo-app/
├── CMakeLists.txt              # Сборка CMake
├── logo.png                    # Логотип приложения
├── scripts/
│   └── convert_icon.ps1        # Конвертация PNG → ICO (PowerShell)
├── src/
│   ├── main.cpp                # Основной исходный код
│   ├── resource.h              # Идентификаторы ресурсов
│   └── app.rc                  # Файл ресурсов (иконка, меню)
└── README.md
```

## Сборка

```bash
cmake -B build -G "Ninja" 
cmake --build build --config Release
```

> При первом запуске CMake автоматически конвертирует `logo.png` → `src/logo.ico` с помощью PowerShell-скрипта `scripts/convert_icon.ps1`.

Собранный исполняемый файл: `build/Release/rbpo-app.exe`

### Запуск

```bash
# Обычный запуск (главное окно показывается)
./build/Release/rbpo-app.exe

# Скрытый запуск (только иконка в трее)
./build/Release/rbpo-app.exe --silent
```

---

## Реализация требований

### 1. Иконка в области уведомлений при запуске

Функция `AddTrayIcon()` вызывается в `wWinMain` сразу после создания окна. Используется `Shell_NotifyIconW(NIM_ADD, ...)`.

- [`src/main.cpp:91-92`](src/main.cpp#L91-L92) — вызов `AddTrayIcon` при старте
- [`src/main.cpp:211-224`](src/main.cpp#L211-L224) — реализация `AddTrayIcon()`

### 2. Клик ЛКМ по иконке → показ главного окна

Обработка `WM_LBUTTONUP` в callback-сообщении `WM_TRAYICON`.

- [`src/main.cpp:131-134`](src/main.cpp#L131-L134) — обработка левого клика
- [`src/main.cpp:251-256`](src/main.cpp#L251-L256) — реализация `ShowMainWindow()`

### 3. Клик ПКМ по иконке → контекстное меню

Обработка `WM_RBUTTONUP` — вызов `ShowTrayContextMenu()`, которая создаёт popup-меню через `CreatePopupMenu` / `TrackPopupMenu`.

- [`src/main.cpp:135-138`](src/main.cpp#L135-L138) — обработка правого клика
- [`src/main.cpp:232-248`](src/main.cpp#L232-L248) — реализация `ShowTrayContextMenu()`

### 4. Пункт «Открыть» в контекстном меню

Пункт добавляется через `AppendMenuW` с идентификатором `ID_TRAY_OPEN`. При клике вызывается `ShowMainWindow()`.

- [`src/main.cpp:238`](src/main.cpp#L238) — добавление пункта «Открыть»
- [`src/main.cpp:145-148`](src/main.cpp#L145-L148) — обработка `ID_TRAY_OPEN`
- [`src/resource.h:11`](src/resource.h#L11) — определение `ID_TRAY_OPEN`

### 5. Пункт «Выход» в контекстном меню

Пункт добавляется через `AppendMenuW` с идентификатором `ID_TRAY_EXIT`. При клике вызывается `DestroyWindow()`.

- [`src/main.cpp:240`](src/main.cpp#L240) — добавление пункта «Выход»
- [`src/main.cpp:149-152`](src/main.cpp#L149-L152) — обработка `ID_TRAY_EXIT`
- [`src/resource.h:12`](src/resource.h#L12) — определение `ID_TRAY_EXIT`

### 6. Пересоздание панели задач → повторное добавление иконки

Регистрируется системное сообщение `TaskbarCreated` через `RegisterWindowMessageW`. При получении этого сообщения иконка добавляется заново.

- [`src/main.cpp:55-56`](src/main.cpp#L55-L56) — регистрация `WM_TASKBARCREATED`
- [`src/main.cpp:120-124`](src/main.cpp#L120-L124) — обработка пересоздания панели задач

### 7. Запуск без показа главного окна

Поддерживается флаг командной строки `--silent`. Окно создаётся скрытым; если флаг не передан — окно показывается.

- [`src/main.cpp:79-84`](src/main.cpp#L79-L84) — создание окна (изначально скрыто)
- [`src/main.cpp:94-98`](src/main.cpp#L94-L98) — проверка флага `--silent`

### 8. Закрытие окна → работа в фоне

Обработчик `WM_CLOSE` скрывает окно вместо его уничтожения. Приложение продолжает работу в трее.

- [`src/main.cpp:160-163`](src/main.cpp#L160-L163) — перехват `WM_CLOSE`, вызов `HideMainWindow()`
- [`src/main.cpp:259-262`](src/main.cpp#L259-L262) — реализация `HideMainWindow()`

### 9. Меню «Файл» → «Выход»

Меню главного окна определено в файле ресурсов `app.rc` и назначено через `wc.lpszMenuName`. Пункт «Выход» обрабатывается по `ID_FILE_EXIT`.

- [`src/app.rc:6-12`](src/app.rc#L6-L12) — определение меню «Файл» → «Выход»
- [`src/main.cpp:67`](src/main.cpp#L67) — привязка меню к окну
- [`src/main.cpp:153-156`](src/main.cpp#L153-L156) — обработка `ID_FILE_EXIT`

### 10. Однократный запуск (single-instance)

Используется именованный мьютекс `Local\RBPO_TrayApp_SingleInstance`. При обнаружении повторного запуска приложение завершается **до** создания иконки в трее.

- [`src/main.cpp:18`](src/main.cpp#L18) — имя мьютекса
- [`src/main.cpp:46-51`](src/main.cpp#L46-L51) — проверка и досрочный выход
- [`src/main.cpp:108-110`](src/main.cpp#L108-L110) — освобождение мьютекса при завершении

### 11. Сборка с использованием CMake

Проект полностью собирается через CMake. Поддерживаются генераторы Visual Studio, Ninja и NMake.

- [`CMakeLists.txt`](CMakeLists.txt) — полная конфигурация сборки
- [`scripts/convert_icon.ps1`](scripts/convert_icon.ps1) — автоматическая конвертация иконки

---

## Технологии

| Компонент | Технология |
|-----------|-----------|
| Язык | C++17 |
| API | Win32 API (`windows.h`, `shellapi.h`) |
| Сборка | CMake ≥ 3.20 |
| Компилятор | MSVC (Visual Studio) |
