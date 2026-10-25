// Manual original-frame capture helper, built separately without a CRT.
// Suspend only the verified game thread before detaching WineDbg so that the
// presentation workers can finish the already submitted frame.
// Usage: reference-thread-control.exe s|r EXPECTED_WINDOWS_PID_HEX TID_HEX
// Success exits16+previous suspend count; errors1..5 never overlap success.

typedef unsigned long DWORD;
typedef void* HANDLE;

char* __stdcall GetCommandLineA(void);
HANDLE __stdcall OpenThread(DWORD access, int inherit, DWORD thread_id);
DWORD __stdcall GetProcessIdOfThread(HANDLE thread);
DWORD __stdcall SuspendThread(HANDLE thread);
DWORD __stdcall ResumeThread(HANDLE thread);
int __stdcall CloseHandle(HANDLE handle);
__declspec(noreturn) void __stdcall ExitProcess(unsigned int code);

static int space(char value) {
    return value == ' ' || value == '\t';
}

static const char* skip_space(const char* text) {
    while (space(*text)) {
        ++text;
    }
    return text;
}

static const char* skip_program(const char* text) {
    int quoted = 0, characters = 0;
    text = skip_space(text);
    while (*text && (quoted || !space(*text))) {
        if (*text == '"') {
            quoted = !quoted;
        } else {
            characters = 1;
        }
        ++text;
    }
    if (quoted || !characters) {
        ExitProcess(4);
    }
    return skip_space(text);
}

static DWORD hex_id(const char** cursor) {
    const char* text = skip_space(*cursor);
    DWORD value = 0;
    unsigned int digits = 0;
    while (*text && !space(*text)) {
        unsigned int digit;
        if (*text >= '0' && *text <= '9') {
            digit = (unsigned int)(*text - '0');
        } else if (*text >= 'a' && *text <= 'f') {
            digit = (unsigned int)(*text - 'a') + 10;
        } else if (*text >= 'A' && *text <= 'F') {
            digit = (unsigned int)(*text - 'A') + 10;
        } else {
            ExitProcess(4);
        }
        if (++digits > 8) {
            ExitProcess(4);
        }
        value = (value << 4) | digit;
        ++text;
    }
    if (!digits || !value) {
        ExitProcess(4);
    }
    *cursor = text;
    return value;
}

void __cdecl entry(void) {
    const char* text = GetCommandLineA();
    if (!text) {
        ExitProcess(4);
    }
    text = skip_program(text);
    if ((*text != 's' && *text != 'r') || !space(text[1])) {
        ExitProcess(4);
    }
    const int resume = *text == 'r';
    ++text;
    const DWORD expected_process = hex_id(&text);
    const DWORD thread_id = hex_id(&text);
    if (*skip_space(text)) {
        ExitProcess(4);
    }

    // THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION. Query the opened
    // object before changing its suspend count, guarding against ID reuse.
    HANDLE thread = OpenThread(0x42, 0, thread_id);
    if (!thread) {
        ExitProcess(2);
    }
    const DWORD process = GetProcessIdOfThread(thread);
    if (!process || process != expected_process) {
        CloseHandle(thread);
        ExitProcess(process ? 3 : 1);
    }
    const DWORD previous = resume ? ResumeThread(thread) : SuspendThread(thread);
    const int closed = CloseHandle(thread);
    if (previous == 0xffffffffUL || !closed) {
        ExitProcess(1);
    }
    // Win32's maximum suspend count is127; keep the exit status unambiguous
    // even if an unexpected API result is returned.
    if (previous > 127) {
        ExitProcess(5);
    }
    ExitProcess(16 + previous);
}
