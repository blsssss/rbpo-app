# РБПО — Windows Service + Tray Application

Windows-служба и графическое приложение (трей), взаимодействующие через **Windows RPC** с транспортом **ALPC**. Реализовано на **C++17 / Win32 API**, система сборки — **CMake**.

## Структура проекта

```
rbpo-app/
├── CMakeLists.txt                  # Сборка CMake (GUI + Service + MIDL)
├── logo.png                        # Логотип приложения
├── scripts/
│   └── convert_icon.ps1            # Конвертация PNG → ICO (PowerShell)
├── src/
│   ├── main.cpp                    # GUI-приложение (tray)
│   ├── resource.h                  # Идентификаторы ресурсов
│   ├── app.rc                      # Файл ресурсов (иконка, меню)
│   ├── rbpo_rpc_constants.h        # Общие константы (имена, endpoint)
│   ├── rpc/
│   │   ├── rbpo_rpc.idl            # IDL-описание RPC-интерфейса
│   │   └── rbpo_rpc.acf            # ACF (implicit binding handle)
│   └── service/
│       └── service_main.cpp        # Windows-служба
└── README.md
```

## Сборка

Сборка выполняется из **Developer Command Prompt for VS** (чтобы `midl.exe` был доступен):

```bash
cmake -B build -G "Ninja"
cmake --build build --config Release
```

> При первом запуске CMake автоматически конвертирует `logo.png` → `src/logo.ico`.

Результат сборки:
- `build/rbpo-app.exe` — графическое приложение
- `build/rbpo-service.exe` — Windows-служба

## Регистрация и запуск службы

```powershell
# Регистрация службы (от имени администратора)
sc.exe create RBPOService binPath= "C:\полный\путь\к\rbpo-service.exe" start= demand

# Запуск / остановка
sc.exe start RBPOService
# Остановка выполняется через RPC (из GUI-приложения)

# Удаление службы
sc.exe delete RBPOService
```

---

## Реализация требований к Windows-службе

### С-1. Запуск GUI во всех терминальных сессиях (кроме сессии 0)

При старте служба перечисляет активные сессии (`WTSEnumerateSessionsW`) и запускает `rbpo-app.exe --silent` в каждой (кроме 0) от имени владельца сессии (`WTSQueryUserToken` + `CreateProcessAsUserW`). Главное окно скрыто.

- [`src/service/service_main.cpp`](src/service/service_main.cpp) — `LaunchAppInSession()`, вызов в `ServiceMain()`

### С-2. Отслеживание входов новых пользователей

Служба принимает `SERVICE_CONTROL_SESSIONCHANGE` / `WTS_SESSION_LOGON` и запускает GUI в новой сессии.

- [`src/service/service_main.cpp`](src/service/service_main.cpp) — `ServiceCtrlHandlerEx()`, обработка `WTS_SESSION_LOGON`

### С-3. Отключение обработки Stop и Shutdown

В `dwControlsAccepted` не устанавливаются флаги `SERVICE_ACCEPT_STOP` и `SERVICE_ACCEPT_SHUTDOWN`. Обработчик возвращает `NO_ERROR` без каких-либо действий.

- [`src/service/service_main.cpp`](src/service/service_main.cpp) — `ServiceCtrlHandlerEx()`, ветки `SERVICE_CONTROL_STOP` / `SERVICE_CONTROL_SHUTDOWN`

### С-4. RPC-сервер (ALPC)

Служба регистрирует транспорт `ncalrpc` с endpoint `RBPOServiceEndpoint` и вызывает `RpcServerListen` (блокирующий). Служба работает до вызова `RpcMgmtStopServerListening`.

- [`src/service/service_main.cpp`](src/service/service_main.cpp) — `ServiceMain()`, секция RPC
- [`src/rpc/rbpo_rpc.idl`](src/rpc/rbpo_rpc.idl) — IDL-описание интерфейса
- [`src/rpc/rbpo_rpc.acf`](src/rpc/rbpo_rpc.acf) — ACF с implicit handle

### С-5. RPC-интерфейс для остановки

Интерфейс `RBPOServiceRpc` содержит метод `RBPOService_Stop()`, который вызывает `RpcMgmtStopServerListening`, завершая работу RPC-сервера и службы.

- [`src/service/service_main.cpp`](src/service/service_main.cpp) — реализация `RBPOService_Stop()`

### С-6. Завершение всех GUI-приложений при остановке

После возврата из `RpcServerListen` служба вызывает `TerminateProcess` для каждого запущенного дочернего процесса.

- [`src/service/service_main.cpp`](src/service/service_main.cpp) — `TerminateAllChildren()`

---

## Реализация требований к графическому приложению

### GUI-1. Проверка состояния службы при запуске

Если служба не запущена, приложение запускает её (`StartServiceW`), ожидает состояния `SERVICE_RUNNING` (до 30 сек) и завершается.

- [`src/main.cpp`](src/main.cpp) — `IsServiceRunning()`, `StartServiceAndWait()`, проверка в `wWinMain()`

### GUI-2. Проверка родительского процесса

Приложение определяет PID родительского процесса через `CreateToolhelp32Snapshot` и проверяет имя исполняемого файла (`QueryFullProcessImageNameW`). Если это не `rbpo-service.exe`, приложение завершается.

- [`src/main.cpp`](src/main.cpp) — `GetParentProcessId()`, `IsParentService()`, проверка в `wWinMain()`

### GUI-3. «Выход» в главном меню → остановка службы

Пункт «Файл → Выход» (`ID_FILE_EXIT`) вызывает `StopServiceViaRpc()` перед `DestroyWindow`.

- [`src/main.cpp`](src/main.cpp) — обработка `ID_FILE_EXIT` в `WndProc()`

### GUI-4. «Выход» в контекстном меню трея → остановка службы

Пункт «Выход» (`ID_TRAY_EXIT`) вызывает `StopServiceViaRpc()` перед `DestroyWindow`.

- [`src/main.cpp`](src/main.cpp) — обработка `ID_TRAY_EXIT` в `WndProc()`

---

## Межпроцессное взаимодействие (RPC / ALPC)

| Параметр | Значение |
|----------|----------|
| Транспорт | `ncalrpc` (ALPC) |
| Endpoint | `RBPOServiceEndpoint` |
| Интерфейс | `RBPOServiceRpc` v1.0 |
| UUID | `A1B2C3D4-E5F6-7890-ABCD-EF1234567890` |
| Binding | Implicit (`hRBPOServiceBinding`) |

Клиент (GUI) создаёт binding через `RpcStringBindingComposeW` + `RpcBindingFromStringBindingW` и вызывает `RBPOService_Stop()`. Сервер (служба) реализует эту функцию, останавливая RPC-сервер.

---

## Технологии

| Компонент | Технология |
|-----------|-----------|
| Язык | C++17 |
| API | Win32 API, WTS API, RPC API |
| IPC | Windows RPC (ALPC / `ncalrpc`) |
| IDL | MIDL compiler |
| Сборка | CMake ≥ 3.20 |
| Компилятор | MSVC (Visual Studio) |
