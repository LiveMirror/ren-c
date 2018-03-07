//
//  File: %host-main.c
//  Summary: "Host environment main entry point"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
#include "tmp-boot-extensions.h"

EXTERN_C REBOL_HOST_LIB Host_Lib_Init;


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
    EXTERN_C HINSTANCE App_Instance;
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
            App_Instance = GetModuleHandle(NULL);
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
            if (this_exe_path == NULL) { // argc was > 1
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
                if (!CreateProcess(
                    NULL, // lpApplicationName
                    this_exe_path, // lpCommandLine
                    NULL, // lpProcessAttributes
                    NULL, // lpThreadAttributes
                    FALSE, // bInheritHandles
                    CREATE_DEFAULT_ERROR_MODE | DETACHED_PROCESS,
                    NULL, // lpEnvironment
                    NULL, // lpCurrentDirectory
                    &startinfo,
                    &procinfo
                )){
                    MessageBox(
                        NULL, // owner window
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
REBOOL ctrl_c_enabled = TRUE;


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

    ctrl_c_enabled = FALSE;
}

void Enable_Ctrl_C(void)
{
    assert(NOT(ctrl_c_enabled));

    SetConsoleCtrlHandler(Handle_Break, TRUE);
    SetConsoleCtrlHandler(Handle_Nothing, FALSE);

    ctrl_c_enabled = TRUE;
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

    sigaction(SIGINT, NULL, &old_action); // fetch current handler
    if (old_action.sa_handler != SIG_IGN) {
        struct sigaction new_action;
        new_action.sa_handler = SIG_IGN;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, NULL);
    }

    ctrl_c_enabled = FALSE;
}

void Enable_Ctrl_C(void)
{
    assert(NOT(ctrl_c_enabled));

    if (old_action.sa_handler != SIG_IGN) {
        struct sigaction new_action;
        new_action.sa_handler = &Handle_Signal;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, NULL);
    }

    ctrl_c_enabled = TRUE;
}

#endif


// Can't just use a TRAP when running user code, because it might legitimately
// evaluate to an ERROR! value, as well as FAIL.  Uses rebRescue().

struct sandbox_info {
    REBVAL *group_or_block;
    REBVAL *result;
};

REBVAL *Run_Sandboxed_Code(struct sandbox_info *info) {
    //
    // !!! Change this to rebRun("lib/do", ...) when binding logic is fixed,
    // so the argument doesn't get rebound.
    //
    info->result = rebDoValue(info->group_or_block);
    return rebBlank(); // distinct type from rebRescue() ERROR! trapping
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

    rebStartup(&Host_Lib_Init);

    // With interpreter startup done, we want to turn the platform-dependent
    // argument strings into a block of Rebol strings as soon as possible.
    // That way the command line argument processing can be taken care of by
    // PARSE in the HOST-STARTUP user function, instead of C code!
    //
    REBVAL *argv_block = rebBlock(END);

  #ifdef TO_WINDOWS
    //
    // Were we using WinMain we'd be getting our arguments in Unicode, but
    // since we're using an ordinary main() we do not.  However, this call
    // lets us slip out and pick up the arguments in Unicode form (UCS2).
    //
    WCHAR **argv_ucs2 = CommandLineToArgvW(GetCommandLineW(), &argc);
    UNUSED(argv_ansi);

    Determine_Hinstance_May_Respawn(argc > 1 ? NULL : argv_ucs2[0]);

    int i;
    for (i = 0; i < argc; ++i) {
        if (argv_ucs2[i] == NULL)
            continue; // !!! Comment here said "shell bug" (?)

        REBVAL *arg = rebStringW(argv_ucs2[i]);
        rebElide("append", argv_block, arg, END);
        rebRelease(arg);
    }
  #else
    // Just take the ANSI C "char*" args...which should ideally be in UTF8.
    //
    int i = 0;
    for (; i < argc; ++i) {
        if (argv_ansi[i] == NULL)
            continue; // !!! Comment here said "shell bug" (?)

        REBVAL *arg = rebString(argv_ansi[i]);
        rebElide("append", argv_block, arg, END);
        rebRelease(arg);
    }
  #endif

    // !!! This calls into the internal API for decompression, instead of
    // turning the data into a BINARY! and then using rebRun("decompress"...)
    // on it.  It would be wasteful to make that intermediate compressed form
    // as a binary, which raises the question of if there should be a
    // rebDecompress() (or rebSizedDecompress()?) entry point.

    const REBOOL gzip = FALSE;
    const REBOOL raw = FALSE;
    const REBOOL only = FALSE;
    REBSER *startup = Inflate_To_Series(
        &Reb_Init_Code[0],
        REB_INIT_SIZE,
        -1,
        gzip,
        raw,
        only
    );
    if (startup == NULL)
        panic ("Can't decompress %host-start.r linked into executable");

    REBARR *array = Scan_UTF8_Managed(
        Intern("host-start.r"), BIN_HEAD(startup), BIN_LEN(startup)
    );

    // Bind the REPL and startup code into the lib context.
    //
    // !!! It's important not to load the REPL into user, because since it
    // uses routines like PRINT to do it's I/O you (probably) don't want
    // the REPL to get messed up if PRINT is redefined--for instance.  It
    // should probably have its own context, which would entail a copy of
    // every word in lib that it uses, but that mechanic hasn't been
    // fully generalized--and might not be the right answer anyway.
    //
    // Only add top-level words to the `lib' context
    Bind_Values_Set_Midstream_Shallow(ARR_HEAD(array), Lib_Context);

    // Bind all words to the `lib' context, but not adding any new words
    Bind_Values_Deep(ARR_HEAD(array), Lib_Context);

    // The new policy for source code in Ren-C is that it loads read only.
    // This didn't go through the LOAD Rebol function (should it?  it
    // never did before.)  For now, use simple binding but lock it.
    //
    Deep_Freeze_Array(array);

    DECLARE_LOCAL (host_console);
    if (Do_At_Throws(
        host_console, // returned value must be a FUNCTION!
        array,
        0,
        SPECIFIED
    )){
        panic (startup); // just loads functions, shouldn't QUIT or error
    }

    if (!IS_FUNCTION(host_console))
        rebPanicValue (host_console, END);

    Free_Series(startup);

    DECLARE_LOCAL (ext_value);
    Init_Blank(ext_value);
    LOAD_BOOT_EXTENSIONS(ext_value);

    DECLARE_LOCAL(exec_path);
    REBCHR *path;
    REBINT path_len = OS_GET_CURRENT_EXEC(&path);
    if (path_len < 0)
        Init_Blank(exec_path);
    else {
        Init_File(exec_path,
            To_REBOL_Path(path, path_len, (OS_WIDE ? PATH_OPT_UNI_SRC : 0))
            );
        OS_FREE(path);
    }

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
    // Note that `code`, `result`, and `status` have to be freed each loop ATM.
    //
    REBVAL *code = rebBlank();
    REBVAL *result = rebBlock(exec_path, argv_block, ext_value, END);
    REBVAL *status = rebBlank();

    // The DO and APPLY hooks are used to implement things like tracing
    // or debugging.  If they were allowed to run during the host
    // console, they would create a fair amount of havoc (the console
    // is supposed to be "invisible" and not show up on the stack...as if
    // it were part of the C codebase, even though it isn't written in C)
    //
    REBDOF saved_do_hook = PG_Do;
    REBAPF saved_apply_hook = PG_Apply;

    // !!! While the new mode of TRACE (and other code hooking function
    // execution) is covered by `saved_do_hook` and `saved_apply_hook`, there
    // is independent tracing code in PARSE which is also enabled by TRACE ON
    // and has to be silenced during console-related code.  Review how hooks
    // into PARSE and other services can be avoided by the console itself
    //
    REBINT Save_Trace_Level = Trace_Level;
    REBINT Save_Trace_Depth = Trace_Depth;

    while (TRUE) {
        assert(NOT(ctrl_c_enabled)); // not while HOST-CONSOLE is on the stack

        REBVAL *trapped = rebTrap(
            rebEval(host_console), // HOST-CONSOLE function (run it)
            code, // GROUP! or BLOCK! executed prior (blank if first run)
            result, // result of evaluating previous code (or void if error)
            status, // blank or the ERROR! (maybe uncaught throw/halt/quit)
            END
        );

        if (rebDid("lib/error?", trapped, END))
            rebPanic (trapped, END); // unsafe code should run in a GROUP!

        rebRelease(code);
        rebRelease(result);
        rebRelease(status);

        code = trapped;

        if (rebDid("lib/integer?", code, END))
            break; // when HOST-CONSOLE returns INTEGER! it means an exit code

        REBOOL is_console_instruction = rebDid("lib/block?", code, END);

        // Restore custom DO and APPLY hooks, but only if running a GROUP!.
        // (We do not want to trace/debug/instrument Rebol code that the
        // console is using to implement *itself*, which it does with BLOCK!)
        // Same for Trace_Level seen by PARSE.
        //
        if (NOT(is_console_instruction)) {
            PG_Do = saved_do_hook;
            PG_Apply = saved_apply_hook;
            Trace_Level = Save_Trace_Level;
            Trace_Depth = Save_Trace_Depth;
        }

        // Both GROUP! and BLOCK! code is cancellable with Ctrl-C (though it's
        // up to HOST-CONSOLE on the next iteration to decide whether to
        // accept the cancellation or consider it an error condition or a
        // reason to fall back to the default skin).
        //
        struct sandbox_info info;
        info.group_or_block = code;
        info.result = NULL;

        Enable_Ctrl_C();
        status = rebRescue(cast(REBDNG*, &Run_Sandboxed_Code), &info);
        Disable_Ctrl_C();

        // If the custom DO and APPLY hooks were changed by the user code,
        // then save them...but restore the unhooked versions for the next
        // iteration of HOST-CONSOLE.  Same for Trace_Level seen by PARSE.
        //
        if (NOT(is_console_instruction)) {
            saved_do_hook = PG_Do;
            saved_apply_hook = PG_Apply;
            PG_Do = &Do_Core;
            PG_Apply = &Apply_Core;
            Save_Trace_Level = Trace_Level;
            Save_Trace_Depth = Trace_Depth;
            Trace_Level = 0;
            Trace_Depth = 0;
        }

        if (rebDid("lib/blank?", status, END))
            result = info.result;
        else
            result = rebVoid();
    }

    int exit_status = rebUnboxInteger(code);
    rebRelease(code);

    rebRelease(argv_block);

    SHUTDOWN_BOOT_EXTENSIONS();

    OS_QUIT_DEVICES(0);

    const REBOOL clean = FALSE; // process exiting, not necessary
    rebShutdown(clean); // Note: debug build runs a clean shutdown anyway

    return exit_status; // http://stackoverflow.com/q/1101957/
}
