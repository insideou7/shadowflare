// crash.txt: what went wrong, the call stack and the last log lines, written
// next to the program when the game crashes. Retry in a Debug build's
// assertion dialog only helps with Visual Studio installed; this works
// without it.
#include "platform/crash_log.h"

#include <SDL.h>

#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>

#include "core/log.h"

#if defined(SF_SENTRY)
#include <sentry.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h first: dbghelp.h needs its types.
#include <dbghelp.h>
#include <crtdbg.h>
#elif !defined(__EMSCRIPTEN__) && (defined(__GLIBC__) || defined(__APPLE__))
#include <execinfo.h>
#define SF_HAVE_BACKTRACE 1
#endif

namespace sf::crash {

namespace {

// Found up front: little is safe to call once the program is broken.
char crashPath[1024] = "crash.txt";
#if defined(SF_SENTRY)
bool sentryOn = false;
#endif
// One crash.txt a run: the first failure is the one that matters (a failed
// Debug check is followed by abort() when Abort is pressed).
bool written = false;

#if defined(_WIN32)

void writeFrame(std::FILE* f, HANDLE process, DWORD64 address) {
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 offset = 0;
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD lineOffset = 0;
    const bool named = SymFromAddr(process, address, &offset, symbol);
    const bool lined = SymGetLineFromAddr64(process, address, &lineOffset, &line);
    std::fprintf(f, "  %016llx %s", static_cast<unsigned long long>(address), named ? symbol->Name : "?");
    if (lined) std::fprintf(f, "  %s:%lu", line.FileName, line.LineNumber);
    std::fprintf(f, "\n");
}

void writeStack(std::FILE* f, CONTEXT* context) {
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    std::fprintf(f, "\nCall stack:\n");
    if (context) {
        // The stack of the fault itself, walked from its registers.
        CONTEXT copy = *context;
        STACKFRAME64 frame{};
        DWORD machine = 0;
#if defined(_M_X64)
        machine = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = copy.Rip;
        frame.AddrFrame.Offset = copy.Rbp;
        frame.AddrStack.Offset = copy.Rsp;
#elif defined(_M_IX86)
        machine = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = copy.Eip;
        frame.AddrFrame.Offset = copy.Ebp;
        frame.AddrStack.Offset = copy.Esp;
#elif defined(_M_ARM64)
        machine = IMAGE_FILE_MACHINE_ARM64;
        frame.AddrPC.Offset = copy.Pc;
        frame.AddrFrame.Offset = copy.Fp;
        frame.AddrStack.Offset = copy.Sp;
#endif
        frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 64 && machine; ++i) {
            if (!StackWalk64(machine, process, GetCurrentThread(), &frame, &copy, nullptr, SymFunctionTableAccess64,
                             SymGetModuleBase64, nullptr) ||
                frame.AddrPC.Offset == 0) {
                break;
            }
            writeFrame(f, process, frame.AddrPC.Offset);
        }
    } else {
        void* frames[64];
        const USHORT count = CaptureStackBackTrace(1, 64, frames, nullptr);
        for (USHORT i = 0; i < count; ++i) writeFrame(f, process, reinterpret_cast<DWORD64>(frames[i]));
    }
}

#else

void writeStack(std::FILE* f) {
#if defined(SF_HAVE_BACKTRACE)
    std::fprintf(f, "\nCall stack:\n");
    std::fflush(f);
    void* frames[64];
    const int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, fileno(f));
#else
    (void)f;
#endif
}

#endif

// Opens crash.txt and writes the reason; the caller adds the stack.
std::FILE* begin(const char* reason) {
    if (written) return nullptr;
    written = true;
    std::FILE* f = std::fopen(crashPath, "w");
    if (!f) return nullptr;
    const std::time_t now = std::time(nullptr);
    char when[64] = "";
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &local);
    std::fprintf(f, "ShadowFlare crashed at %s\n%s\n", when, reason);
    return f;
}

void finish(std::FILE* f) {
    const std::string lines = log::recent();
    if (!lines.empty()) std::fprintf(f, "\nLast log lines:\n%s", lines.c_str());
    std::fclose(f);
    std::fprintf(stderr, "[ERROR] The game crashed; details in %s\n", crashPath);
}

#if defined(SF_SENTRY)

// Log lines go to Sentry as breadcrumbs, so a report shows what led to it.
void addBreadcrumb(log::Level level, std::string_view message) {
    sentry_value_t crumb = sentry_value_new_breadcrumb("default", std::string(message).c_str());
    sentry_value_set_by_key(crumb, "level", sentry_value_new_string(level == log::Level::Error  ? "error"
                                                                    : level == log::Level::Warn ? "warning"
                                                                    : level == log::Level::Debug ? "debug"
                                                                                                 : "info"));
    sentry_add_breadcrumb(crumb);
}

// Crashes (signals, Windows exceptions) are caught by Sentry itself; what
// it cannot see, a Debug check or an uncaught exception's message, is sent
// here.
void sendToSentry(const char* reason) {
    if (!sentryOn) return;
    sentry_value_t event = sentry_value_new_message_event(SENTRY_LEVEL_FATAL, "crash", reason);
    sentry_value_set_stacktrace(event, nullptr, 0);
    sentry_capture_event(event);
    sentry_flush(3000);
}

void startSentry(const char* base) {
    const char* dsn = std::getenv("SHADOWFLARE_SENTRY_DSN");
    if (!dsn || !*dsn) dsn = SF_SENTRY_DSN;
    if (!*dsn) return;
    sentry_options_t* options = sentry_options_new();
    sentry_options_set_dsn(options, dsn);
    const std::string database = std::string(base) + ".sentry-native";
    sentry_options_set_database_path(options, database.c_str());
    sentry_options_set_release(options, "shadowflare");
#if defined(NDEBUG)
    sentry_options_set_environment(options, "release");
#else
    sentry_options_set_environment(options, "debug");
#endif
    if (sentry_init(options) != 0) {
        log::warn("Sentry did not start; crashes are only written to {}", crashPath);
        return;
    }
    sentryOn = true;
    std::atexit([] { sentry_close(); });
    log::setListener(addBreadcrumb);
    log::info("Crash reports go to Sentry");
}

#endif

void report(const char* reason) {
    std::FILE* f = begin(reason);
    if (!f) return;
#if defined(_WIN32)
    writeStack(f, nullptr);
#else
    writeStack(f);
#endif
    finish(f);
}

#if defined(_WIN32)

LONG WINAPI onException(EXCEPTION_POINTERS* info) {
    char reason[128];
    std::snprintf(reason, sizeof reason, "Exception 0x%08lx at %p", info->ExceptionRecord->ExceptionCode,
                  info->ExceptionRecord->ExceptionAddress);
    if (std::FILE* f = begin(reason)) {
        writeStack(f, info->ContextRecord);
        finish(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

#if defined(_DEBUG)
// Debug builds' checks ("vector subscript out of range"): written down
// before the dialog shows, whichever button is pressed.
int onCrtReport(int type, char* message, int* returnValue) {
    if (type == _CRT_ASSERT || type == _CRT_ERROR) {
        std::string reason = "Debug check failed: ";
        reason += message ? message : "";
        report(reason.c_str());
#if defined(SF_SENTRY)
        sendToSentry(reason.c_str());
#endif
    }
    (void)returnValue;
    return FALSE;  // let the usual dialog show as well
}
#endif

#endif

void onSignal(int signal) {
    const char* name = signal == SIGSEGV ? "SIGSEGV (bad memory access)"
                       : signal == SIGABRT ? "SIGABRT (abort)"
                       : signal == SIGFPE  ? "SIGFPE (arithmetic error)"
                       : signal == SIGILL  ? "SIGILL (illegal instruction)"
                                           : "signal";
    report(name);
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void onTerminate() {
    std::string reason = "std::terminate";
    if (const auto e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            reason += ": uncaught exception: ";
            reason += ex.what();
        } catch (...) {
            reason += ": uncaught exception";
        }
    }
    report(reason.c_str());
#if defined(SF_SENTRY)
    sendToSentry(reason.c_str());
#endif
    std::signal(SIGABRT, SIG_DFL);
    std::abort();
}

}  // namespace

void install() {
#if !defined(__EMSCRIPTEN__)
    char* base = SDL_GetBasePath();
    std::snprintf(crashPath, sizeof crashPath, "%scrash.txt", base ? base : "");
    std::set_terminate(onTerminate);
    std::signal(SIGSEGV, onSignal);
    std::signal(SIGABRT, onSignal);
    std::signal(SIGFPE, onSignal);
    std::signal(SIGILL, onSignal);
#if defined(_WIN32)
    SetUnhandledExceptionFilter(onException);
#if defined(_DEBUG)
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, onCrtReport);
#endif
#endif
    // After ours, so Sentry's handlers run first and then pass on to ours.
#if defined(SF_SENTRY)
    startSentry(base ? base : "");
#endif
    SDL_free(base);
#endif
}

}  // namespace sf::crash
