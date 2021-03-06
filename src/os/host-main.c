//
//  File: %host-main.c
//  Summary: "Host environment main entry point"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// %host-main.c is the original entry point for the open-sourced R3-Alpha.
// Depending on whether it was POSIX or Windows, it would define either a
// `main()` or `WinMain()`, and implemented a very rudimentary console.
//
// On POSIX systems it uses <termios.h> to implement line editing:
//
// http://pubs.opengroup.org/onlinepubs/7908799/xbd/termios.html
//
// On Windows it uses the Console API:
//
// https://msdn.microsoft.com/en-us/library/ms682087.aspx
//

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
    //
    // On Windows it is required to include <windows.h>, and defining the
    // _WIN32_WINNT constant to 0x0501 specifies the minimum targeted version
    // is Windows XP.  This is the earliest platform API still supported by
    // Visual Studio 2015:
    //
    //     https://msdn.microsoft.com/en-us/library/6sehtctf.aspx
    //
    // R3-Alpha used 0x0500, indicating a minimum target of Windows 2000.  No
    // Windows-XP-specific dependencies were added in Ren-C, but the version
    // was bumped to avoid compilation errors in the common case.
    //
    #undef _WIN32_WINNT
    #define _WIN32_WINNT 0x0501
    #include <windows.h>

    // Put any dependencies that include <windows.h> here
    //
    /* #include "..." */
    /* #include "..." */

    // Undefine the Windows version of IS_ERROR to avoid compiler warning
    // when Rebol redefines it.  (Rebol defines IS_XXX for all datatypes.)
    //
    #undef IS_ERROR
    #undef max
    #undef min
#else
    #include <signal.h> // needed for SIGINT, SIGTERM, SIGHUP
#endif


#include "sys-core.h"
#include "sys-ext.h"


// Initialization done by rebStartup() is intended to be as basic as possible
// in order to get the Rebol series/values/array functions ready to be run.
// Once that's ready, the rest of the initialization can take advantage of
// a working evaluator.  This includes PARSE to process the command line
// parameters, or PRINT to output boot banners.
//
// The %make-host-init.r file takes the %host-start.r script and turns it
// into a compressed binary C literal.  That literal can be LOADed and
// executed to return the HOST-START function, which takes the command line
// arguments as an array of STRING! and handles it from there.
//
#include "tmp-host-start.inc"


#ifdef TO_WINDOWS
    //
    // Most Windows-specific code is expected to be run in extensions (or
    // in the interim, in "devices").  However, it's expected that all Windows
    // code be able to know its `HINSTANCE`.  This is usually passed in a
    // WinMain(), but since we don't use WinMain() in order to be able to
    // act as a console app -or- a GUI app some tricks are needed to capture
    // it, and then export it for other code to use.
    //
    // !!! This is not currently exported via EXTERN_C, because the core was
    // building in a dependency on the host.  This created problems for the
    // libRebol, which needs to be independent of %host-main.c, and may be
    // used with clients that do not have the HINSTANCE easily available.
    // The best idea for exporting it is probably to have those clients who
    // provide it to inject it into the system object as a HANDLE!, so that
    // those extensions which need it have access to it, while not creating
    // problems for those that do not.
    //
    HINSTANCE App_Instance = 0;

    // For why this is done this way with a potential respawning, see the
    // StackOverflow question:
    //
    // "Can one executable be both a console and a GUI application":
    //
    //     http://stackoverflow.com/q/493536/
    //
    void Determine_Hinstance_May_Respawn(WCHAR *this_exe_path) {
        if (GetStdHandle(STD_OUTPUT_HANDLE) == 0) {
            //
            // No console to attach to, we must be the DETACHED_PROCESS which
            // was spawned in the below branch.
            //
            App_Instance = GetModuleHandle(nullptr);
        }
        else {
          #ifdef REB_CORE
            //
            // In "Core" mode, use a console but do not initialize graphics.
            // (stdio redirection works, blinking console window during start)
            //
            App_Instance = cast(HINSTANCE,
                GetWindowLongPtr(GetConsoleWindow(), GWLP_HINSTANCE)
            );
            UNUSED(this_exe_path);
          #else
            //
            // In the "GUI app" mode, stdio redirection doesn't work properly,
            // but no blinking console window during start.
            //
            if (not this_exe_path) { // argc was > 1
                App_Instance = cast(HINSTANCE,
                    GetWindowLongPtr(GetConsoleWindow(), GWLP_HINSTANCE)
                );
            }
            else {
                // Launch child as a DETACHED_PROCESS so that GUI can be
                // initialized, and exit.
                //
                STARTUPINFO startinfo;
                ZeroMemory(&startinfo, sizeof(startinfo));
                startinfo.cb = sizeof(startinfo);

                PROCESS_INFORMATION procinfo;
                if (not CreateProcess(
                    nullptr, // lpApplicationName
                    this_exe_path, // lpCommandLine
                    nullptr, // lpProcessAttributes
                    nullptr, // lpThreadAttributes
                    FALSE, // bInheritHandles
                    CREATE_DEFAULT_ERROR_MODE | DETACHED_PROCESS,
                    nullptr, // lpEnvironment
                    nullptr, // lpCurrentDirectory
                    &startinfo,
                    &procinfo
                )){
                    MessageBox(
                        nullptr, // owner window
                        L"CreateProcess() failed in %host-main.c",
                        this_exe_path, // title
                        MB_ICONEXCLAMATION | MB_OK
                    );
                }

                exit(0);
            }
          #endif
        }
    }
#endif


// Assume that Ctrl-C is enabled in a console application by default.
// (Technically it may be set to be ignored by a parent process or context,
// in which case conventional wisdom is that we should not be enabling it
// ourselves.)
//
bool ctrl_c_enabled = true;


#ifdef TO_WINDOWS

//
// This is the callback passed to `SetConsoleCtrlHandler()`.
//
BOOL WINAPI Handle_Break(DWORD dwCtrlType)
{
    switch (dwCtrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        rebHalt();
        return TRUE; // TRUE = "we handled it"

    case CTRL_CLOSE_EVENT:
        //
        // !!! Theoretically the close event could confirm that the user
        // wants to exit, if there is possible unsaved state.  As a UI
        // premise this is probably less good than persisting the state
        // and bringing it back.
        //
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        //
        // They pushed the close button, did a shutdown, etc.  Exit.
        //
        // !!! Review arbitrary "100" exit code here.
        //
        exit(100);

    default:
        return FALSE; // FALSE = "we didn't handle it"
    }
}

BOOL WINAPI Handle_Nothing(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT)
        return TRUE;

    return FALSE;
}

void Disable_Ctrl_C(void)
{
    assert(ctrl_c_enabled);

    SetConsoleCtrlHandler(Handle_Break, FALSE);
    SetConsoleCtrlHandler(Handle_Nothing, TRUE);

    ctrl_c_enabled = false;
}

void Enable_Ctrl_C(void)
{
    assert(not ctrl_c_enabled);

    SetConsoleCtrlHandler(Handle_Break, TRUE);
    SetConsoleCtrlHandler(Handle_Nothing, FALSE);

    ctrl_c_enabled = true;
}

#else

// SIGINT is the interrupt usually tied to "Ctrl-C".  Note that if you use
// just `signal(SIGINT, Handle_Signal);` as R3-Alpha did, this means that
// blocking read() calls will not be interrupted with EINTR.  One needs to
// use sigaction() if available...it's a slightly newer API.
//
// http://250bpm.com/blog:12
//
// !!! What should be done about SIGTERM ("polite request to end", default
// unix kill) or SIGHUP ("user's terminal disconnected")?  Is it useful to
// register anything for these?  R3-Alpha did, and did the same thing as
// SIGINT.  Not clear why.  It did nothing for SIGQUIT:
//
// SIGQUIT is used to terminate a program in a way that is designed to
// debug it, e.g. a core dump.  Receiving SIGQUIT is a case where
// program exit functions like deletion of temporary files may be
// skipped to provide more state to analyze in a debugging scenario.
//
// SIGKILL is the impolite signal for shutdown; cannot be hooked/blocked

static void Handle_Signal(int sig)
{
    UNUSED(sig);
    rebHalt();
}

struct sigaction old_action;

void Disable_Ctrl_C(void)
{
    assert(ctrl_c_enabled);

    sigaction(SIGINT, nullptr, &old_action); // fetch current handler
    if (old_action.sa_handler != SIG_IGN) {
        struct sigaction new_action;
        new_action.sa_handler = SIG_IGN;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, nullptr);
    }

    ctrl_c_enabled = false;
}

void Enable_Ctrl_C(void)
{
    assert(not ctrl_c_enabled);

    if (old_action.sa_handler != SIG_IGN) {
        struct sigaction new_action;
        new_action.sa_handler = &Handle_Signal;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, nullptr);
    }

    ctrl_c_enabled = true;
}

#endif


// Can't just use a TRAP when running user code, because it might legitimately
// evaluate to an ERROR! value, as well as FAIL.  Uses rebRescue().

REBVAL *Run_Sandboxed_Code(REBVAL *group_or_block) {
    //
    // Don't want to use DO here, because that would add an extra stack
    // level of Rebol ACTION! in the backtrace.  See notes on rebRunInline()
    // for its possible future.
    //
    REBVAL *result = rebRunInline(group_or_block);
    if (not result)
        return result; // ownership will be proxied

    return rebRun("[", rebR(result), "]", rebEND); // ownership gets proxied
}


//=//// MAIN ENTRY POINT //////////////////////////////////////////////////=//
//
// Using a main() entry point for a console program (as opposed to WinMain())
// so we can connect to the console.  See Determine_Hinstance_May_Respawn().
//
int main(int argc, char *argv_ansi[])
{
    // We only enable Ctrl-C when user code is running...not when the
    // HOST-CONSOLE function itself is, or during startup.  (Enabling it
    // during startup would require a special "kill" mode that did not
    // call rebHalt(), as basic startup cannot meaningfully be halted.)
    //
    Disable_Ctrl_C();

    rebStartup();

    // With interpreter startup done, we want to turn the platform-dependent
    // argument strings into a block of Rebol strings as soon as possible.
    // That way the command line argument processing can be taken care of by
    // PARSE in the HOST-STARTUP user function, instead of C code!
    //
    REBVAL *argv_block = rebRun("lib/copy []", rebEND);

  #ifdef TO_WINDOWS
    //
    // Were we using WinMain we'd be getting our arguments in Unicode, but
    // since we're using an ordinary main() we do not.  However, this call
    // lets us slip out and pick up the arguments in Unicode form (UTF-16).
    //
    WCHAR **argv_ucs2 = CommandLineToArgvW(GetCommandLineW(), &argc);
    UNUSED(argv_ansi);

    Determine_Hinstance_May_Respawn(argc > 1 ? nullptr : argv_ucs2[0]);

    int i;
    for (i = 0; i != argc; ++i) {
        if (argv_ucs2[i] == nullptr)
            continue; // !!! Comment here said "shell bug" (?)

        // Note: rebTextW() currently only supports UCS-2, so codepoints that
        // need more than two bytes to be represented will cause a failure.
        //
        rebElide("append", argv_block, rebR(rebTextW(argv_ucs2[i])), rebEND);
    }
  #else
    // Just take the ANSI C "char*" args...which should ideally be in UTF8.
    //
    int i = 0;
    for (; i != argc; ++i) {
        if (argv_ansi[i] == nullptr)
            continue; // !!! Comment here said "shell bug" (?)

        rebElide("append", argv_block, rebT(argv_ansi[i]), rebEND);
    }
  #endif

    size_t host_utf8_size;
    const int max = -1; // decompressed size is stored in gzip
    void *host_utf8_bytes = rebGunzipAlloc(
        &host_utf8_size,
        &Reb_Init_Code[0],
        REB_INIT_SIZE,
        max
    );

    // The inflated data was allocated with rebMalloc, and hence can be
    // repossessed as a BINARY!
    //
    REBVAL *host_bin = rebRepossess(host_utf8_bytes, host_utf8_size);

    // Use TRANSCODE to get a BLOCK! from the BINARY!, then release the binary
    //
    REBVAL *host_code = rebRun(
        "lib/transcode/file", host_bin, "%tmp-host-start.inc", rebEND
    );
    rebElide(
        "lib/ensure :lib/empty? lib/take/last", host_code, // empty bin @ tail
        rebEND
    );
    rebRelease(host_bin);

    // Create a new context specifically for the console.  This way, changes
    // to the user context should hopefully not affect it...e.g. if the user
    // redefines PRINT in their script, the console should keep working.
    //
    // !!! In the API source here calling methods textually, the current way
    // of insulating by using lib, e.g. `rebRun("lib/error?", ...)`, is still
    // using *the user context's notion of `lib`*.  So if they said `lib: 10`
    // then the console would die.  General API point to consider, as the
    // design emerges.
    //
    REBCTX *console_ctx = Alloc_Context_Core(
        REB_OBJECT,
        80,
        NODE_FLAG_MANAGED // no PUSH_GC_GUARD needed, gets referenced
    );

    // Bind words that can be found in lib context (don't add any new words)
    //
    // !!! Directly binding to lib means that the console *could* screw up and
    // overwrite lib declarations.  It should probably import its own copy,
    // just in case.  (Lib should also be protected by default)
    //
    Bind_Values_Deep(VAL_ARRAY_HEAD(host_code), Lib_Context);

    // Do two passes on the console context.  One to find SET-WORD!s at the
    // top level and add them to the context, and another pass to deeply bind
    // to those declarations.
    //
    Bind_Values_Set_Midstream_Shallow(VAL_ARRAY_HEAD(host_code), console_ctx);
    Bind_Values_Deep(VAL_ARRAY_HEAD(host_code), console_ctx);

    // The new policy for source code in Ren-C is that it loads read only.
    // This didn't go through the LOAD Rebol action or anything like it, so
    // go ahead and lock it manually.
    //
    // !!! This file is supposed to be based on libRebol APIs, and the method
    // of creating a new context here is low level using the internal API.
    // However the console context is created should ideally be done in a
    // way that would work well for users, by leveraging modules or some other
    // level of abstraction, where issues like this would be taken care of.
    //
    rebElide("lib/lock", host_code, rebEND);

    REBVAL *host_console = rebRunInline(host_code); // console is an ACTION!
    rebRelease(host_code);

    if (rebNot("lib/action?", host_console, rebEND))
        rebJumps("lib/PANIC-VALUE", host_console, rebEND);

    // The config file used by %make.r marks extensions to be built into the
    // executable (`+`), built as a dynamic library (`*`), or not built at
    // all (`-`).  Each of the options marked with + has a C function for
    // startup and shutdown, which we convert into HANDLE!s to be suitable
    // to pass into the Rebol startup code--which chooses the actual moment
    // to call LOAD-EXTENSION on them.
    //
    REBVAL *extensions = rebBuiltinExtensions();

    // While some people may think that argv[0] in C contains the path to
    // the running executable, this is not necessarily the case.  The actual
    // method for getting the current executable path is OS-specific:
    //
    // https://stackoverflow.com/q/1023306/
    // http://stackoverflow.com/a/933996/211160
    //
    // It's not foolproof, so it might come back blank.  The console code can
    // then decide if it wants to fall back on argv[0]
    //
    REBVAL *exec_path = OS_GET_CURRENT_EXEC();
    rebElide(
        "system/options/boot: lib/ensure [blank! file!]", rebR(exec_path),
        rebEND
    );

    // !!! Previously the C code would call a separate startup function
    // explicitly.  This created another difficult case to bulletproof
    // various forms of failures during service routines that were already
    // being handled by the framework surrounding HOST-CONSOLE.  The new
    // approach is to let HOST-CONSOLE be the sole entry point, and that
    // PRIOR code being blank is an indication that it is running for the
    // first time.  Thus it can use that opportunity to run any startup
    // code or print any banners it wishes.
    //
    // However, the previous call to the startup function gave it three
    // explicit parameters.  The parameters might best be passed by
    // sticking them in the environment somewhere and letting HOST-CONSOLE
    // find them...but for the moment we pass them as a BLOCK! in the
    // RESULT argument when the PRIOR code is blank, and let it unpack them.
    //
    // Note that `code`, and `result` have to be released each loop ATM.
    //
    REBVAL *code = rebBlank();
    REBVAL *result = rebRun("[", argv_block, extensions, "]", rebEND);

    // References in the `result` BLOCK! keep the underlying series alive now
    //
    rebRelease(argv_block);

    // The DO and APPLY hooks are used to implement things like tracing
    // or debugging.  If they were allowed to run during the host
    // console, they would create a fair amount of havoc (the console
    // is supposed to be "invisible" and not show up on the stack...as if
    // it were part of the C codebase, even though it isn't written in C)
    //
    REBDOF saved_eval_hook = PG_Eval;
    REBDSF saved_dispatcher_hook = PG_Dispatcher;

    // !!! While the new mode of TRACE (and other code hooking function
    // execution) is covered by `saved_eval_hook/saved_apply_hook`, there
    // is independent tracing code in PARSE which is also enabled by TRACE ON
    // and has to be silenced during console-related code.  Review how hooks
    // into PARSE and other services can be avoided by the console itself
    //
    REBINT Save_Trace_Level = Trace_Level;
    REBINT Save_Trace_Depth = Trace_Depth;

    bool no_recover = false; // allow one try at HOST-CONSOLE internal error

    while (true) {
        assert(not ctrl_c_enabled); // not while HOST-CONSOLE is on the stack

    recover:;

        // This runs the HOST-CONSOLE, which returns *requests* to execute
        // arbitrary code by way of its return results.  The TRAP and CATCH
        // are thus here to intercept bugs *in HOST-CONSOLE itself*.  Any
        // evaluations for the user (or on behalf of the console skin) are
        // done in Run_Sandboxed_Code().
        //
        REBVAL *trapped = rebRun(
            "lib/entrap [",
                host_console, // action! that takes 3 args, run it
                rebUneval(code), // group!/block! executed prior (or blank!)
                rebUneval(result), // prior result in a block, or error/null
            "]", rebEND
        );

        rebRelease(code);
        rebRelease(result);

        if (rebDid("lib/error?", trapped, rebEND)) {
            //
            // If the HOST-CONSOLE function has any of its own implementation
            // that could raise an error (or act as an uncaught throw) it
            // *should* be returned as a BLOCK!.  This way the "console skin"
            // can be reset to the default.  If HOST-CONSOLE itself fails
            // (e.g. a typo in the implementation) there's probably not much
            // use in trying again...but give it a chance rather than just
            // crash.  Pass it back something that looks like an instruction
            // it might have generated (a BLOCK!) asking itself to crash.

            if (no_recover)
                rebJumps("lib/PANIC", trapped, rebEND);

            code = rebRun("[#host-console-error]", rebEND);
            result = trapped;
            no_recover = true; // no second chances until user code runs
            goto recover;
        }

        code = rebRun("lib/first", trapped, rebEND); // entrap []'s the output
        rebRelease(trapped); // don't need the outer block any more

        if (rebDid("lib/integer?", code, rebEND))
            break; // when HOST-CONSOLE returns INTEGER! it means an exit code

        bool is_console_instruction = rebDid("lib/block?", code, rebEND);

        // Restore custom DO and APPLY hooks, but only if running a GROUP!.
        // (We do not want to trace/debug/instrument Rebol code that the
        // console is using to implement *itself*, which it does with BLOCK!)
        // Same for Trace_Level seen by PARSE.
        //
        if (not is_console_instruction) {
            //
            // If they made it to a user mode instruction, re-enable recovery.
            //
            no_recover = false;

            PG_Eval = saved_eval_hook;
            PG_Dispatcher = saved_dispatcher_hook;
            Trace_Level = Save_Trace_Level;
            Trace_Depth = Save_Trace_Depth;
        }

        // Both GROUP! and BLOCK! code is cancellable with Ctrl-C (though it's
        // up to HOST-CONSOLE on the next iteration to decide whether to
        // accept the cancellation or consider it an error condition or a
        // reason to fall back to the default skin).
        //
        Enable_Ctrl_C();
        result = rebRescue(cast(REBDNG*, &Run_Sandboxed_Code), code);
        Disable_Ctrl_C();

        // If the custom DO and APPLY hooks were changed by the user code,
        // then save them...but restore the unhooked versions for the next
        // iteration of HOST-CONSOLE.  Same for Trace_Level seen by PARSE.
        //
        if (not is_console_instruction) {
            saved_eval_hook = PG_Eval;
            saved_dispatcher_hook = PG_Dispatcher;
            PG_Eval = &Eval_Core;
            PG_Dispatcher = &Dispatcher_Core;
            Save_Trace_Level = Trace_Level;
            Save_Trace_Depth = Trace_Depth;
            Trace_Level = 0;
            Trace_Depth = 0;
        }
    }

    rebRelease(host_console);

    int exit_status = rebUnboxInteger(rebR(code), rebEND);

    // This calls the QUIT functions of the extensions loaded at boot, in the
    // reverse order of initialization.  (It does not call unload-extension,
    // because marking native stubs as "missing" for safe errors if they
    // are called is not necessary, since the whole system is exiting.)
    //
    rebShutdownExtensions(extensions);
    rebRelease(extensions);

    OS_QUIT_DEVICES(0);

    const bool clean = false; // process exiting, not necessary
    rebShutdown(clean); // Note: debug build runs a clean shutdown anyway

    return exit_status; // http://stackoverflow.com/q/1101957/
}
