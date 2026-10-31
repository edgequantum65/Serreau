#include "chess_engine.h"
#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <string.h>

static HANDLE hInputRead  = NULL;
static HANDLE hInputWrite = NULL;
static HANDLE hOutputRead = NULL;
static HANDLE hOutputWrite = NULL;
static HANDLE hEngineThread = NULL;
static CRITICAL_SECTION csOutput;
static volatile BOOL bInitialized = FALSE;

__declspec(dllexport) void engine_write(const char *cmd);

static DWORD WINAPI EngineThreadProc(LPVOID lpParam) {
    (void)lpParam;

    int input_fd  = _open_osfhandle((intptr_t)hInputRead,  _O_RDONLY);
    int output_fd = _open_osfhandle((intptr_t)hOutputWrite, _O_WRONLY);

    FILE *fIn  = _fdopen(input_fd,  "r");
    FILE *fOut = _fdopen(output_fd, "w");

    setbuf(fOut, NULL);
    g_uci_out = fOut;

    uci_loop_ex(fIn, fOut);

    fclose(fIn);
    fclose(fOut);
    g_uci_out = NULL;
    return 0;
}

__declspec(dllexport) BOOL engine_start(void) {
    if (bInitialized) return TRUE;

    InitializeCriticalSection(&csOutput);

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    if (!CreatePipe(&hInputRead, &hInputWrite, &sa, 0))
        return FALSE;
    if (!CreatePipe(&hOutputRead, &hOutputWrite, &sa, 0))
        return FALSE;

    SetHandleInformation(hInputWrite,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutputRead,  HANDLE_FLAG_INHERIT, 0);

    init_engine();

    hEngineThread = CreateThread(NULL, 0, EngineThreadProc, NULL, 0, NULL);
    if (!hEngineThread) return FALSE;

    bInitialized = TRUE;
    return TRUE;
}

__declspec(dllexport) void engine_stop(void) {
    if (!bInitialized) return;

    engine_write("stop");
    Sleep(100);
    engine_write("quit");

    WaitForSingleObject(hEngineThread, 5000);

    CloseHandle(hEngineThread);
    CloseHandle(hInputRead);
    CloseHandle(hInputWrite);
    CloseHandle(hOutputRead);
    CloseHandle(hOutputWrite);

    DeleteCriticalSection(&csOutput);
    bInitialized = FALSE;
}

__declspec(dllexport) BOOL engine_is_running(void) {
    if (!bInitialized) return FALSE;
    DWORD exitCode;
    if (!GetExitCodeThread(hEngineThread, &exitCode)) return FALSE;
    return (exitCode == STILL_ACTIVE);
}

__declspec(dllexport) void engine_write(const char *cmd) {
    if (!bInitialized || !cmd) return;
    DWORD written;
    WriteFile(hInputWrite, cmd, (DWORD)strlen(cmd), &written, NULL);
    WriteFile(hInputWrite, "\n", 1, &written, NULL);
}

__declspec(dllexport) int engine_read(char *buf, int max_len) {
    if (!bInitialized || !buf || max_len <= 0) return 0;

    EnterCriticalSection(&csOutput);

    DWORD available = 0;
    if (!PeekNamedPipe(hOutputRead, NULL, 0, NULL, &available, NULL) || available == 0) {
        LeaveCriticalSection(&csOutput);
        return 0;
    }

    if ((int)available > max_len - 1)
        available = (DWORD)(max_len - 1);

    DWORD bytesRead = 0;
    if (!ReadFile(hOutputRead, buf, available, &bytesRead, NULL)) {
        LeaveCriticalSection(&csOutput);
        return 0;
    }

    buf[bytesRead] = '\0';
    LeaveCriticalSection(&csOutput);
    return (int)bytesRead;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL;
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_DETACH) {
        if (bInitialized) {
            engine_write("stop");
            engine_write("quit");
            WaitForSingleObject(hEngineThread, 2000);
        }
    }
    return TRUE;
}
