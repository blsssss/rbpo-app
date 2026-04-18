/**
 * РБПО — Windows Service
 *
 * Launches the tray application in every active terminal session (except 0),
 * monitors new user logons via SESSION_CHANGE notifications,
 * exposes an RPC interface over ALPC for remote stop,
 * and terminates all child processes on service shutdown.
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstdio>

#include "rbpo_rpc_h.h"
#include "rbpo_rpc_constants.h"

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "advapi32.lib")

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static SERVICE_STATUS_HANDLE g_hServiceStatus = nullptr;
static SERVICE_STATUS        g_ServiceStatus  = {};
static std::vector<HANDLE>   g_childProcesses;
static CRITICAL_SECTION      g_cs;

// ---------------------------------------------------------------------------
// Diagnostic log — writes to rbpo-service.log next to the exe
// ---------------------------------------------------------------------------
static void Log(const char* fmt, ...)
{
    static char logPath[MAX_PATH] = {};
    if (!logPath[0]) {
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string p(exePath);
        auto pos = p.find_last_of("\\/");
        if (pos != std::string::npos) p = p.substr(0, pos + 1);
        p += "rbpo-service.log";
        strncpy_s(logPath, p.c_str(), _TRUNCATE);
    }
    FILE* f = nullptr;
    fopen_s(&f, logPath, "a");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

// ---------------------------------------------------------------------------
// RPC memory allocation (required by the RPC runtime)
// ---------------------------------------------------------------------------
void* __RPC_USER midl_user_allocate(size_t size) { return malloc(size); }
void  __RPC_USER midl_user_free(void* p)         { free(p); }

// ---------------------------------------------------------------------------
// RPC interface implementation — Requirement 5
// Clients call this to stop the service.
// ---------------------------------------------------------------------------
void RBPOService_Stop(void)
{
    Log("RBPOService_Stop called by RPC client");
    RpcMgmtStopServerListening(nullptr);
}

// ---------------------------------------------------------------------------
// Get path to the GUI application (same directory as the service exe)
// ---------------------------------------------------------------------------
static std::wstring GetAppPath()
{
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    auto pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        path = path.substr(0, pos + 1);
    path += RBPO_APP_EXE_NAME;
    return path;
}

// ---------------------------------------------------------------------------
// Launch the GUI app in a given terminal session — Requirements 1, 2
//   • Skips session 0
//   • Runs as the session owner
//   • Main window is hidden (--silent)
// ---------------------------------------------------------------------------
static void LaunchAppInSession(DWORD sessionId)
{
    if (sessionId == 0) return;

    HANDLE hToken = nullptr;
    if (!WTSQueryUserToken(sessionId, &hToken)) {
        Log("  WTSQueryUserToken failed for session %u, error=%u", sessionId, GetLastError());
        return;
    }

    HANDLE hDupToken = nullptr;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, nullptr,
                          SecurityIdentification, TokenPrimary, &hDupToken)) {
        Log("  DuplicateTokenEx failed, error=%u", GetLastError());
        CloseHandle(hToken);
        return;
    }
    CloseHandle(hToken);

    LPVOID pEnv = nullptr;
    CreateEnvironmentBlock(&pEnv, hDupToken, FALSE);

    std::wstring appPath = GetAppPath();
    std::wstring cmdLine = L"\"" + appPath + L"\" --silent";

    STARTUPINFOW si = { sizeof(si) };
    si.lpDesktop   = const_cast<LPWSTR>(L"winsta0\\default");
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    // writable copy of the command line (CreateProcessAsUser requirement)
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    if (CreateProcessAsUserW(hDupToken, appPath.c_str(), cmdBuf.data(),
                             nullptr, nullptr, FALSE,
                             CREATE_UNICODE_ENVIRONMENT,
                             pEnv, nullptr, &si, &pi)) {
        Log("  Launched PID=%u in session %u", pi.dwProcessId, sessionId);
        EnterCriticalSection(&g_cs);
        g_childProcesses.push_back(pi.hProcess);
        LeaveCriticalSection(&g_cs);
        CloseHandle(pi.hThread);
    } else {
        Log("  CreateProcessAsUserW failed, error=%u", GetLastError());
    }

    if (pEnv) DestroyEnvironmentBlock(pEnv);
    CloseHandle(hDupToken);
}

// ---------------------------------------------------------------------------
// Terminate all launched GUI applications — Requirement 6
// ---------------------------------------------------------------------------
static void TerminateAllChildren()
{
    EnterCriticalSection(&g_cs);
    for (HANDLE h : g_childProcesses) {
        TerminateProcess(h, 0);
        CloseHandle(h);
    }
    g_childProcesses.clear();
    LeaveCriticalSection(&g_cs);
}

// ---------------------------------------------------------------------------
// Service control handler
//   • Requirement 3: ignores SERVICE_CONTROL_STOP and SERVICE_CONTROL_SHUTDOWN
//   • Requirement 2: launches app on WTS_SESSION_LOGON
// ---------------------------------------------------------------------------
static DWORD WINAPI ServiceCtrlHandlerEx(DWORD dwControl, DWORD dwEventType,
                                          LPVOID lpEventData, LPVOID)
{
    switch (dwControl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        // Requirement 3: disable Stop and Shutdown handling
        return NO_ERROR;

    case SERVICE_CONTROL_SESSIONCHANGE:
        // Requirement 2: launch app when a new user logs on
        if (dwEventType == WTS_SESSION_LOGON) {
            auto* pNotify = reinterpret_cast<WTSSESSION_NOTIFICATION*>(lpEventData);
            LaunchAppInSession(pNotify->dwSessionId);
        }
        return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// ServiceMain — entry point called by the SCM
// ---------------------------------------------------------------------------
static void WINAPI ServiceMain(DWORD, LPWSTR*)
{
    InitializeCriticalSection(&g_cs);

    Log("=== ServiceMain started (PID=%u) ===", GetCurrentProcessId());

    g_hServiceStatus = RegisterServiceCtrlHandlerExW(
        RBPO_SERVICE_NAME, ServiceCtrlHandlerEx, nullptr);
    if (!g_hServiceStatus) { Log("RegisterServiceCtrlHandlerExW failed"); return; }

    // Report SERVICE_START_PENDING
    g_ServiceStatus.dwServiceType      = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState     = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_SESSIONCHANGE;
    // NOTE: SERVICE_ACCEPT_STOP and SERVICE_ACCEPT_SHUTDOWN are deliberately omitted
    SetServiceStatus(g_hServiceStatus, &g_ServiceStatus);

    // --- Requirement 1: launch app in all existing active sessions -----------
    WTS_SESSION_INFOW* pSessions = nullptr;
    DWORD sessionCount = 0;
    if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1,
                               &pSessions, &sessionCount)) {
        Log("Enumerated %u sessions", sessionCount);
        for (DWORD i = 0; i < sessionCount; i++) {
            Log("  Session %u: id=%u state=%d",
                i, pSessions[i].SessionId, pSessions[i].State);
            if (pSessions[i].State == pSessions[i].SessionId != 0)
                LaunchAppInSession(pSessions[i].SessionId);
        }
        WTSFreeMemory(pSessions);
    } else {
        Log("WTSEnumerateSessionsW failed, error=%u", GetLastError());
    }

    // --- Requirement 4: start RPC server with ALPC transport -----------------
    RPC_STATUS rpcStatus;
    rpcStatus = RpcServerUseProtseqEpW(
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"ncalrpc")),
        RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(RBPO_RPC_ENDPOINT)),
        nullptr);

    Log("RpcServerUseProtseqEpW returned %d", rpcStatus);
    if (rpcStatus != RPC_S_OK) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = rpcStatus;
        SetServiceStatus(g_hServiceStatus, &g_ServiceStatus);
        DeleteCriticalSection(&g_cs);
        return;
    }

    // --- Requirement 5: register the RPC interface ---------------------------
    rpcStatus = RpcServerRegisterIf(RBPOServiceRpc_v1_0_s_ifspec, nullptr, nullptr);
    Log("RpcServerRegisterIf returned %d", rpcStatus);
    if (rpcStatus != RPC_S_OK) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = rpcStatus;
        SetServiceStatus(g_hServiceStatus, &g_ServiceStatus);
        DeleteCriticalSection(&g_cs);
        return;
    }

    // Report SERVICE_RUNNING
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_hServiceStatus, &g_ServiceStatus);
    Log("Service RUNNING, entering RpcServerListen...");

    // --- Requirement 4: blocks until RpcMgmtStopServerListening is called ----
    rpcStatus = RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, FALSE);
    Log("RpcServerListen returned %d", rpcStatus);

    // --- Requirement 6: terminate all child processes on stop ----------------
    TerminateAllChildren();

    RpcServerUnregisterIf(RBPOServiceRpc_v1_0_s_ifspec, nullptr, FALSE);

    // Report SERVICE_STOPPED
    Log("Service STOPPED");
    g_ServiceStatus.dwCurrentState     = SERVICE_STOPPED;
    g_ServiceStatus.dwControlsAccepted = 0;
    SetServiceStatus(g_hServiceStatus, &g_ServiceStatus);

    DeleteCriticalSection(&g_cs);
}

// ---------------------------------------------------------------------------
// main — dispatches the service to the SCM
// ---------------------------------------------------------------------------
int wmain()
{
    SERVICE_TABLE_ENTRYW serviceTable[] = {
        { const_cast<LPWSTR>(RBPO_SERVICE_NAME), ServiceMain },
        { nullptr, nullptr }
    };
    StartServiceCtrlDispatcherW(serviceTable);
    return 0;
}
