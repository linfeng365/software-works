#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <process.h>
#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <wctype.h>

#define WM_PROGRESS (WM_APP + 1)
#define WM_RESULT   (WM_APP + 2)
#define WM_DONE     (WM_APP + 3)
#define ID_CHOOSE 101
#define ID_REVEAL 102
#define ID_ANGLE 103
#define ID_DIRECTION 104

typedef struct { wchar_t **paths; unsigned count; double angle; BOOL clockwise; } Batch;
typedef struct { wchar_t *text; wchar_t *output; } Result;
typedef struct { unsigned successes, failures, duplicates; } Summary;

static HWND windowHandle, chooseButton, revealButton, statusLabel, progressBar, logView;
static HWND angleField, directionChoice;
static HINSTANCE instance;
static HFONT normalFont, titleFont;
static BOOL busy = FALSE;
static double scale = 1.0;
static wchar_t rootPath[32768];
static wchar_t **outputs;
static unsigned outputCount;

static wchar_t *copy_text(const wchar_t *text) {
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    wchar_t *result = malloc(bytes);
    if (result) memcpy(result, text, bytes);
    return result;
}

static wchar_t *join_path(const wchar_t *suffix) {
    size_t length = wcslen(rootPath) + wcslen(suffix) + 1;
    wchar_t *result = calloc(length, sizeof(wchar_t));
    if (result) swprintf(result, length, L"%ls%ls", rootPath, suffix);
    return result;
}

static const wchar_t *file_name(const wchar_t *path) {
    const wchar_t *end = wcsrchr(path, L'\\');
    return end ? end + 1 : path;
}

/* Quote one argument for the Microsoft C runtime parser; no command shell. */
static wchar_t *quote_argument(const wchar_t *argument) {
    wchar_t *result = malloc((2 * wcslen(argument) + 3) * sizeof(wchar_t));
    if (!result) return NULL;
    wchar_t *out = result;
    *out++ = L'"';
    while (*argument) {
        size_t slashes = 0;
        while (*argument == L'\\') { slashes++; argument++; }
        size_t count = (*argument == L'"' || !*argument) ? slashes * 2 : slashes;
        while (count--) *out++ = L'\\';
        if (*argument == L'"') *out++ = L'\\';
        if (*argument) *out++ = *argument++;
    }
    *out++ = L'"';
    *out = 0;
    return result;
}

static wchar_t *system_error(DWORD code) {
    wchar_t *message = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, (wchar_t *)&message, 0, NULL);
    wchar_t *result = copy_text(message ? message : L"无法启动 PDF 处理引擎。请完整解压工具后重试。");
    if (message) LocalFree(message);
    return result;
}

static BOOL run_python(const wchar_t *argument, double angle, BOOL clockwise, DWORD *exitCode, wchar_t **transcript) {
    BOOL ok = FALSE;
    wchar_t *python = join_path(L"\\runtime\\python.exe");
    wchar_t *script = join_path(L"\\resources\\rotate_pdf.py");
    wchar_t *quotedPython = python ? quote_argument(python) : NULL;
    wchar_t *quotedScript = script ? quote_argument(script) : NULL;
    wchar_t *quotedArgument = quote_argument(argument);
    wchar_t *command = NULL;
    HANDLE readPipe = NULL, writePipe = NULL, input = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION process = {0};
    SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    char *bytes = NULL;
    size_t length = 0, capacity = 0;
    DWORD errorCode = ERROR_NOT_ENOUGH_MEMORY;
    *exitCode = 1;
    *transcript = NULL;
    if (!python || !script || !quotedPython || !quotedScript || !quotedArgument) goto cleanup;
    if (GetFileAttributesW(python) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(script) == INVALID_FILE_ATTRIBUTES) {
        errorCode = ERROR_FILE_NOT_FOUND;
        goto cleanup;
    }
    size_t commandLength = wcslen(quotedPython) + wcslen(quotedScript) + wcslen(quotedArgument) + 120;
    command = calloc(commandLength, sizeof(wchar_t));
    if (!command) goto cleanup;
    swprintf(command, commandLength, L"%ls -I -X utf8 -B %ls --angle %.17g --direction %ls %ls",
             quotedPython, quotedScript, angle, clockwise ? L"cw" : L"ccw", quotedArgument);
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) { errorCode = GetLastError(); goto cleanup; }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (input == INVALID_HANDLE_VALUE) { errorCode = GetLastError(); goto cleanup; }
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    if (!CreateProcessW(python, command, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, rootPath, &startup, &process)) {
        errorCode = GetLastError();
        goto cleanup;
    }
    CloseHandle(writePipe); writePipe = NULL;
    CloseHandle(input); input = INVALID_HANDLE_VALUE;
    char buffer[4096];
    DWORD received;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &received, NULL) && received) {
        /* Always drain the pipe; cap stored diagnostics at 1 MiB. */
        size_t take = received;
        if (length + take > 1048576) take = 1048576 - length;
        if (!take) continue;
        if (length + take + 1 > capacity) {
            size_t newCapacity = length + take + 4097;
            char *newBytes = realloc(bytes, newCapacity);
            if (!newBytes) continue;
            bytes = newBytes; capacity = newCapacity;
        }
        memcpy(bytes + length, buffer, take);
        length += take;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, exitCode);
    if (bytes && length) {
        bytes[length] = 0;
        int size = MultiByteToWideChar(CP_UTF8, 0, bytes, (int)length, NULL, 0);
        *transcript = calloc((size_t)size + 1, sizeof(wchar_t));
        if (*transcript) MultiByteToWideChar(CP_UTF8, 0, bytes, (int)length, *transcript, size);
    }
    if (!*transcript) *transcript = copy_text(L"");
    ok = TRUE;
cleanup:
    if (!ok) *transcript = system_error(errorCode);
    if (readPipe) CloseHandle(readPipe);
    if (writePipe) CloseHandle(writePipe);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (process.hThread) CloseHandle(process.hThread);
    if (process.hProcess) CloseHandle(process.hProcess);
    free(python); free(script); free(quotedPython); free(quotedScript);
    free(quotedArgument); free(command); free(bytes);
    return ok;
}

static void free_batch(Batch *batch) {
    if (!batch) return;
    for (unsigned i = 0; i < batch->count; i++) free(batch->paths[i]);
    free(batch->paths); free(batch);
}

static unsigned __stdcall worker(void *parameter) {
    Batch *batch = parameter;
    Summary *summary = calloc(1, sizeof(Summary));
    if (!summary) { free_batch(batch); PostMessageW(windowHandle, WM_DONE, 0, 0); return 1; }
    for (unsigned i = 0; i < batch->count; i++) {
        size_t size = wcslen(batch->paths[i]) + 160;
        wchar_t *status = calloc(size, sizeof(wchar_t));
        if (status) {
            swprintf(status, size, L"正在处理 %u / %u：%ls", i + 1, batch->count, file_name(batch->paths[i]));
            PostMessageW(windowHandle, WM_PROGRESS, i + 1, (LPARAM)status);
        }
        BOOL duplicate = FALSE;
        for (unsigned j = 0; j < i; j++)
            if (!_wcsicmp(batch->paths[i], batch->paths[j])) { duplicate = TRUE; break; }
        Result *result = calloc(1, sizeof(Result));
        if (!result) { summary->failures++; continue; }
        if (duplicate) {
            summary->duplicates++;
            result->text = copy_text(L"已跳过本批次中的重复输入。");
        } else {
            DWORD code;
            wchar_t *text = NULL;
            BOOL ran = run_python(batch->paths[i], batch->angle, batch->clockwise, &code, &text);
            if (!text) text = copy_text(L"内存不足。");
            size_t n = wcslen(text);
            while (n && (text[n-1] == L'\r' || text[n-1] == L'\n')) text[--n] = 0;
            wchar_t *lastLine = wcsrchr(text, L'\n');
            lastLine = lastLine ? lastLine + 1 : text;
            if (ran && code == 0 && *lastLine && GetFileAttributesW(lastLine) != INVALID_FILE_ATTRIBUTES) {
                summary->successes++;
                result->output = copy_text(lastLine);
                size_t textSize = wcslen(batch->paths[i]) + wcslen(lastLine) + 60;
                result->text = calloc(textSize, sizeof(wchar_t));
                if (result->text) swprintf(result->text, textSize, L"成功：%ls\r\n→ %ls",
                                          file_name(batch->paths[i]), file_name(lastLine));
            } else {
                summary->failures++;
                size_t textSize = wcslen(batch->paths[i]) + wcslen(text) + 80;
                result->text = calloc(textSize, sizeof(wchar_t));
                if (result->text) swprintf(result->text, textSize, L"未处理：%ls\r\n%ls",
                                          file_name(batch->paths[i]), *text ? text : L"处理引擎未返回有效输出。");
            }
            free(text);
        }
        PostMessageW(windowHandle, WM_RESULT, 0, (LPARAM)result);
    }
    free_batch(batch);
    PostMessageW(windowHandle, WM_DONE, 0, (LPARAM)summary);
    return 0;
}

static BOOL read_rotation(double *angle, BOOL *clockwise) {
    wchar_t input[128], *end;
    if (GetWindowTextLengthW(angleField) >= 128) goto invalid;
    GetWindowTextW(angleField, input, 128);
    for (wchar_t *p = input; *p; p++) if (*p == L',') *p = L'.';
    *angle = wcstod(input, &end);
    if (end == input) goto invalid;
    while (iswspace(*end)) end++;
    if (*end || !isfinite(*angle) || *angle < 0.001 || *angle > 180.0) goto invalid;
    *clockwise = SendMessageW(directionChoice, CB_GETCURSEL, 0, 0) == 1;
    return TRUE;
invalid:
    MessageBoxW(windowHandle, L"请输入 0.001° 至 180° 之间的角度，例如 0.5、1 或 1.25。\r\n请通过方向选项选择逆时针或顺时针。",
                L"请输入有效角度", MB_OK | MB_ICONINFORMATION);
    SetFocus(angleField);
    return FALSE;
}

static void start_batch(Batch *batch) {
    if (!batch || !batch->count) { free_batch(batch); return; }
    if (busy) {
        MessageBoxW(windowHandle, L"当前批次正在处理，完成后可以继续添加。", L"PDF 旋转工具", MB_OK | MB_ICONINFORMATION);
        free_batch(batch); return;
    }
    if (!read_rotation(&batch->angle, &batch->clockwise)) { free_batch(batch); return; }
    busy = TRUE;
    for (unsigned i = 0; i < outputCount; i++) free(outputs[i]);
    free(outputs); outputs = NULL; outputCount = 0;
    EnableWindow(chooseButton, FALSE); EnableWindow(revealButton, FALSE);
    EnableWindow(angleField, FALSE); EnableWindow(directionChoice, FALSE);
    EnableMenuItem(GetSystemMenu(windowHandle, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
    wchar_t description[160];
    swprintf(description, 160, L"本批次：%ls %.12g°\r\n\r\n", batch->clockwise ? L"顺时针" : L"逆时针", batch->angle);
    SetWindowTextW(logView, description);
    SetWindowTextW(statusLabel, L"准备处理…");
    SendMessageW(progressBar, PBM_SETRANGE32, 0, batch->count);
    SendMessageW(progressBar, PBM_SETPOS, 0, 0);
    uintptr_t thread = _beginthreadex(NULL, 0, worker, batch, 0, NULL);
    if (thread) CloseHandle((HANDLE)thread);
    else {
        free_batch(batch); busy = FALSE;
        EnableWindow(chooseButton, TRUE);
        EnableWindow(angleField, TRUE); EnableWindow(directionChoice, TRUE);
        EnableMenuItem(GetSystemMenu(windowHandle, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);
        SetWindowTextW(statusLabel, L"无法启动处理线程，请重试。");
    }
}

static void choose_files(void) {
    wchar_t *buffer = calloc(65536, sizeof(wchar_t));
    if (!buffer) return;
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = windowHandle;
    dialog.lpstrFilter = L"PDF 文件 (*.pdf)\0*.pdf\0\0";
    dialog.lpstrTitle = L"选择要旋转的 PDF（可多选）";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = 65536;
    dialog.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) {
        Batch *batch = calloc(1, sizeof(Batch));
        if (batch) {
            wchar_t *next = buffer + wcslen(buffer) + 1;
            unsigned count = 0;
            if (!*next) count = 1;
            else for (wchar_t *p = next; *p; p += wcslen(p) + 1) count++;
            batch->paths = calloc(count, sizeof(wchar_t *));
            if (batch->paths) {
                if (!*next) {
                    wchar_t *path = copy_text(buffer);
                    if (path) batch->paths[batch->count++] = path;
                }
                else for (wchar_t *p = next; *p; p += wcslen(p) + 1) {
                    size_t length = wcslen(buffer) + wcslen(p) + 2;
                    wchar_t *path = calloc(length, sizeof(wchar_t));
                    if (path) { swprintf(path, length, L"%ls\\%ls", buffer, p); batch->paths[batch->count++] = path; }
                }
                start_batch(batch);
            } else free(batch);
        }
    } else if (CommDlgExtendedError()) {
        MessageBoxW(windowHandle, L"选取文件失败；文件很多时，可以分批选择，或直接拖入窗口。",
                    L"PDF 旋转工具", MB_OK | MB_ICONINFORMATION);
    }
    free(buffer);
}

static void reveal_outputs(void) {
    if (!outputCount) return;
    size_t size = wcslen(outputs[0]) + 32;
    wchar_t *parameters = calloc(size, sizeof(wchar_t));
    if (!parameters) return;
    swprintf(parameters, size, L"/select,\"%ls\"", outputs[0]);
    ShellExecuteW(windowHandle, L"open", L"explorer.exe", parameters, NULL, SW_SHOWNORMAL);
    free(parameters);
}

static int px(int units) { return (int)(units * scale + 0.5); }

static HWND control(const wchar_t *kind, const wchar_t *title, DWORD style,
                    int x, int y, int width, int height, int id, HFONT font) {
    HWND handle = CreateWindowExW(0, kind, title, WS_CHILD | WS_VISIBLE | style,
                                  px(x), px(y), px(width), px(height), windowHandle,
                                  (HMENU)(INT_PTR)id, instance, NULL);
    SendMessageW(handle, WM_SETFONT, (WPARAM)font, TRUE);
    return handle;
}

static LRESULT CALLBACK drop_forward(HWND handle, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR subclass, DWORD_PTR data) {
    (void)subclass; (void)data;
    if (message == WM_DROPFILES) return SendMessageW(windowHandle, message, wParam, lParam);
    return DefSubclassProc(handle, message, wParam, lParam);
}

static void append_log(const wchar_t *text) {
    if (!text) return;
    int length = GetWindowTextLengthW(logView);
    SendMessageW(logView, EM_SETSEL, length, length);
    SendMessageW(logView, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageW(logView, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n\r\n");
    SendMessageW(logView, EM_SCROLLCARET, 0, 0);
}

static LRESULT CALLBACK window_proc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            windowHandle = handle;
            normalFont = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            titleFont = CreateFontW(-px(29), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            control(L"STATIC", L"将 PDF 拖到这里", 0, 28, 25, 650, 48, 0, titleFont);
            control(L"STATIC", L"先设置角度，再拖入 PDF · 可多选 · 另存副本", 0, 30, 84, 650, 25, 0, normalFont);
            control(L"STATIC", L"保留原件与完整页边；输出在原文件旁，重名自动编号。", 0, 30, 122, 650, 25, 0, normalFont);
            control(L"STATIC", L"旋转方向", 0, 30, 170, 75, 25, 0, normalFont);
            directionChoice = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                                      105, 162, 125, 150, ID_DIRECTION, normalFont);
            SendMessageW(directionChoice, CB_ADDSTRING, 0, (LPARAM)L"逆时针");
            SendMessageW(directionChoice, CB_ADDSTRING, 0, (LPARAM)L"顺时针");
            SendMessageW(directionChoice, CB_SETCURSEL, 0, 0);
            control(L"STATIC", L"角度", 0, 252, 170, 45, 25, 0, normalFont);
            angleField = control(L"EDIT", L"1", WS_BORDER | ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP,
                                 300, 163, 90, 30, ID_ANGLE, normalFont);
            control(L"STATIC", L"°", 0, 399, 170, 22, 25, 0, normalFont);
            control(L"STATIC", L"支持小数，如 0.5、1.25", 0, 433, 170, 250, 25, 0, normalFont);
            chooseButton = control(L"BUTTON", L"选择 PDF…", BS_PUSHBUTTON | WS_TABSTOP, 30, 216, 130, 36, ID_CHOOSE, normalFont);
            revealButton = control(L"BUTTON", L"显示结果", BS_PUSHBUTTON | WS_TABSTOP, 175, 216, 120, 36, ID_REVEAL, normalFont);
            EnableWindow(revealButton, FALSE);
            statusLabel = control(L"STATIC", L"等待添加 PDF", SS_PATHELLIPSIS, 30, 268, 650, 25, 0, normalFont);
            progressBar = control(PROGRESS_CLASSW, L"", 0, 30, 303, 650, 10, 0, normalFont);
            logView = control(L"EDIT", L"默认逆时针 1°。可修改方向与角度，再拖入或选择 PDF。\r\n本批所有页面使用同一设置；支持 0.001° 至 180°。",
                               WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                               30, 332, 650, 184, 0, normalFont);
            control(L"STATIC", L"v1.1.0  ·  by：linfeng365", 0, 30, 536, 650, 24, 0, normalFont);
            SendMessageW(logView, EM_SETLIMITTEXT, 4194304, 0);
            DragAcceptFiles(handle, TRUE);
            DragAcceptFiles(logView, TRUE);
            SetWindowSubclass(logView, drop_forward, 1, 0);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_CHOOSE && !busy) choose_files();
            if (LOWORD(wParam) == ID_REVEAL) reveal_outputs();
            return 0;
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wParam;
            Batch *batch = calloc(1, sizeof(Batch));
            unsigned count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
            if (batch) {
                batch->paths = calloc(count, sizeof(wchar_t *));
                if (batch->paths) for (unsigned i = 0; i < count; i++) {
                    UINT length = DragQueryFileW(drop, i, NULL, 0);
                    wchar_t *path = calloc((size_t)length + 1, sizeof(wchar_t));
                    if (path) { DragQueryFileW(drop, i, path, length + 1); batch->paths[batch->count++] = path; }
                }
            }
            DragFinish(drop);
            start_batch(batch);
            return 0;
        }
        case WM_PROGRESS:
            SetWindowTextW(statusLabel, (wchar_t *)lParam);
            free((void *)lParam);
            SendMessageW(progressBar, PBM_SETPOS, wParam, 0);
            return 0;
        case WM_RESULT: {
            Result *result = (Result *)lParam;
            append_log(result->text);
            if (result->output) {
                wchar_t **items = realloc(outputs, (outputCount + 1) * sizeof(wchar_t *));
                if (items) { outputs = items; outputs[outputCount++] = result->output; }
                else free(result->output);
            }
            free(result->text); free(result);
            return 0;
        }
        case WM_DONE: {
            Summary *summary = (Summary *)lParam;
            wchar_t text[160];
            if (summary) swprintf(text, 160, L"处理完成：成功 %u 份，失败 %u 份，重复跳过 %u 份。",
                                  summary->successes, summary->failures, summary->duplicates);
            else wcscpy(text, L"处理未完成：内存不足，请分批重试。");
            SetWindowTextW(statusLabel, text);
            free(summary);
            busy = FALSE;
            EnableWindow(chooseButton, TRUE); EnableWindow(revealButton, outputCount != 0);
            EnableWindow(angleField, TRUE); EnableWindow(directionChoice, TRUE);
            EnableMenuItem(GetSystemMenu(handle, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);
            return 0;
        }
        case WM_CLOSE:
            if (busy) {
                MessageBoxW(handle, L"正在处理 PDF，请等待本批完成后再退出。", L"PDF 旋转工具", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            DestroyWindow(handle); return 0;
        case WM_DESTROY:
            DeleteObject(normalFont); DeleteObject(titleFont);
            for (unsigned i = 0; i < outputCount; i++) free(outputs[i]);
            free(outputs);
            PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(handle, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE application, HINSTANCE previous, PWSTR commandLine, int show) {
    (void)previous; (void)commandLine;
    instance = application;
    DWORD length = GetModuleFileNameW(NULL, rootPath, 32768);
    if (!length || length >= 32768) return 1;
    wchar_t *slash = wcsrchr(rootPath, L'\\');
    if (!slash) return 1;
    *slash = 0;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc == 2 && !wcscmp(argv[1], L"--self-test")) {
        DWORD code = 1; wchar_t *text = NULL;
        BOOL ok = run_python(L"--self-test", 1.0, FALSE, &code, &text);
        free(text); LocalFree(argv);
        return ok ? (int)code : 1;
    }
    SetProcessDPIAware();
    HDC screen = GetDC(NULL);
    scale = (double)GetDeviceCaps(screen, LOGPIXELSX) / 96.0;
    ReleaseDC(NULL, screen);
    INITCOMMONCONTROLSEX common = {sizeof(common), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&common);
    WNDCLASSEXW klass = {0};
    klass.cbSize = sizeof(klass);
    klass.lpfnWndProc = window_proc;
    klass.hInstance = instance;
    klass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    klass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    if (!klass.hIcon) klass.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    klass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    klass.lpszClassName = L"PDFRotationToolV110";
    RegisterClassExW(&klass);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rect = {0, 0, px(710), px(575)};
    AdjustWindowRect(&rect, style, FALSE);
    HWND window = CreateWindowExW(0, klass.lpszClassName, L"PDF 旋转工具", style,
                                 CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                                 rect.bottom - rect.top, NULL, NULL, instance, NULL);
    if (!window) { LocalFree(argv); return 1; }
    ShowWindow(window, show); UpdateWindow(window);
    if (argc > 1) {
        Batch *batch = calloc(1, sizeof(Batch));
        if (batch) {
            batch->paths = calloc((size_t)argc - 1, sizeof(wchar_t *));
            if (batch->paths) for (int i = 1; i < argc; i++) {
                if (!wcscmp(argv[i], L"--angle") && i + 1 < argc) {
                    SetWindowTextW(angleField, argv[++i]); continue;
                }
                if (!wcscmp(argv[i], L"--direction") && i + 1 < argc) {
                    SendMessageW(directionChoice, CB_SETCURSEL, !wcscmp(argv[++i], L"cw") ? 1 : 0, 0); continue;
                }
                wchar_t *path = copy_text(argv[i]);
                if (path) batch->paths[batch->count++] = path;
            }
            start_batch(batch);
        }
    }
    LocalFree(argv);
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return 0;
}
