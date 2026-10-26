/* Developer-only coexistence fixture. Never loads ReBirth or calls exports.
 * Owns a newly created ReWi presence handle while one explicitly named child
 * runs; timeout termination targets ONLY that CreateProcess result handle. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#if !defined(__i386__)
#error Build as x86.
#endif

int wmain(int argc, WCHAR **argv) {
    HANDLE mutex = NULL, child_out_read = NULL, child_out_write = NULL;
    SECURITY_ATTRIBUTES inherit = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION child = {0};
    WCHAR command[32768];
    char output[8192];
    DWORD error = ERROR_SUCCESS, child_exit = STILL_ACTIVE, wait_result;
    DWORD count, used = 0;
    BOOL launched = FALSE, timed_out = FALSE, terminated_child = FALSE;
    BOOL held_new_mutex = FALSE, correct = FALSE;
    int result = 1;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc != 3 || wcschr(argv[1], L'"') || wcschr(argv[2], L'"')) {
        error = ERROR_INVALID_PARAMETER; goto done;
    }
    if (swprintf(command, sizeof(command) / sizeof(command[0]),
                 L"\"%ls\" --no-panel-smoke \"%ls\"", argv[1], argv[2]) < 0) {
        error = ERROR_INSUFFICIENT_BUFFER; goto done;
    }
    SetLastError(ERROR_SUCCESS);
    mutex = CreateMutexA(NULL, FALSE, "ReWi");
    error = GetLastError();
    if (!mutex || error == ERROR_ALREADY_EXISTS) goto done;
    error = ERROR_SUCCESS;
    held_new_mutex = TRUE;
    if (!CreatePipe(&child_out_read, &child_out_write, &inherit, sizeof(output))) {
        error = GetLastError(); goto done;
    }
    if (!SetHandleInformation(child_out_read, HANDLE_FLAG_INHERIT, 0)) {
        error = GetLastError(); goto done;
    }
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = child_out_write;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessW(argv[1], command, NULL, NULL, TRUE, 0,
                        NULL, NULL, &startup, &child)) {
        error = GetLastError(); goto done;
    }
    launched = TRUE;
    CloseHandle(child.hThread); child.hThread = NULL;
    CloseHandle(child_out_write); child_out_write = NULL;
    wait_result = WaitForSingleObject(child.hProcess, 15000);
    if (wait_result != WAIT_OBJECT_0) {
        timed_out = wait_result == WAIT_TIMEOUT;
        error = timed_out ? WAIT_TIMEOUT : GetLastError();
        /* This is precisely the process this fixture just created. */
        terminated_child = TerminateProcess(child.hProcess, 124);
        if (terminated_child) WaitForSingleObject(child.hProcess, 3000);
        goto done;
    }
    if (!GetExitCodeProcess(child.hProcess, &child_exit)) {
        error = GetLastError(); goto done;
    }
    /* Child output is tiny and bounded; retain the mutex until it has exited. */
    while (used + 1 < sizeof(output) &&
           ReadFile(child_out_read, output + used, sizeof(output) - used - 1, &count, NULL) && count)
        used += count;
    output[used] = 0;
    if (used) fwrite(output, 1, used, stdout);
    correct = child_exit == 2 &&
        strstr(output, "\"status\":\"refused_existing_mixer\"") &&
        strstr(output, "\"open_result\":0") &&
        strstr(output, "\"drive_calls\":0") &&
        strstr(output, "\"idle_calls\":0") &&
        strstr(output, "\"device_owned\":false");
    if (correct) result = 0;
    else error = ERROR_INVALID_DATA;
done:
    if (child_out_read) CloseHandle(child_out_read);
    if (child_out_write) CloseHandle(child_out_write);
    if (child.hThread) CloseHandle(child.hThread);
    if (child.hProcess) CloseHandle(child.hProcess);
    if (mutex && !CloseHandle(mutex)) { error = GetLastError(); result = 1; }
    printf("{\"fixture\":\"ReWi_coexistence\",\"correct_refusal\":%s,"
           "\"fixture_loads_engine\":false,\"new_ReWi_handle_held\":%s,"
           "\"child_launched\":%s,\"child_exit_code\":%lu,\"timeout\":%s,"
           "\"terminated_only_created_child\":%s,\"win32_error\":%lu}\n",
           correct ? "true" : "false", held_new_mutex ? "true" : "false",
           launched ? "true" : "false", (unsigned long)child_exit,
           timed_out ? "true" : "false", terminated_child ? "true" : "false",
           (unsigned long)error);
    fflush(NULL);
    return result;
}
