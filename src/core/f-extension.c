//
//  File: %f-extension.c
//  Summary: "support for extensions"
//  Section: functional
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
// !!! Extensions in Ren-C are a redesign from extensions in R3-Alpha.  They
// are a work in progress (and need documentation and cleanup), but have
// been a proof-of-concept for the core idea to be able to write code that
// looks similar to Rebol natives, but can be loaded from a DLL making calls
// back into the executable...or alternately, built directly into the Rebol
// interpreter itself based on a configuration switch.
//
// See the %extensions/ directory for some current (evolving) examples.
//

#include "sys-core.h"

#include "sys-ext.h"


//
//  cleanup_extension_init_handler: C
//
void cleanup_extension_init_handler(const REBVAL *v)
  { UNUSED(v); } // cleanup CFUNC* just serves as an ID for the HANDLE!


//
//  cleanup_extension_quit_handler: C
//
void cleanup_extension_quit_handler(const REBVAL *v)
  { UNUSED(v); } // cleanup CFUNC* just serves as an ID for the HANDLE!


//
//  load-extension-helper: native [
//
//  "Low level extension module loader (for DLLs)."
//
//      path-or-handle [file! handle!]
//          "Path to the extension file or handle to a builtin extension"
//  ]
//
REBNATIVE(load_extension_helper)
//
// Low level extension loader:
//
// 1. Opens the DLL for the extension
// 2. Calls RX_Init() to initialize and get its definition header (REBOL)
// 3. Creates a extension object and returns it
// 4. REBOL code then uses that object to define the extension module
//    including natives, data, exports, etc.
//
// Each extension is defined as DLL with:
//
// RX_Init() - init anything needed
// optional RX_Quit() - cleanup anything needed
{
    INCLUDE_PARAMS_OF_LOAD_EXTENSION_HELPER;

    REBCTX *std_ext_ctx = VAL_CONTEXT(Get_System(SYS_STANDARD, STD_EXTENSION));
    REBCTX *context;

    if (IS_FILE(ARG(path_or_handle))) {
        REBVAL *path = ARG(path_or_handle);

        //Check_Security(SYM_EXTENSION, POL_EXEC, val);

        DECLARE_LOCAL (lib);
        MAKE_Library(lib, REB_LIBRARY, path);

        // check if it's reloading an existing extension
        REBVAL *loaded_exts = CTX_VAR(VAL_CONTEXT(Root_System), SYS_EXTENSIONS);
        if (IS_BLOCK(loaded_exts)) {
            RELVAL *item = VAL_ARRAY_HEAD(loaded_exts);
            for (; NOT_END(item); ++item) {
                //
                // do some sanity checking, just to avoid crashing if
                // system/extensions was messed up

                if (not IS_OBJECT(item)) {
                    DECLARE_LOCAL (bad);
                    Derelativize(bad, item, VAL_SPECIFIER(loaded_exts));
                    fail(Error_Bad_Extension_Raw(bad));
                }

                REBCTX *item_ctx = VAL_CONTEXT(item);
                if (
                    CTX_LEN(item_ctx) <= STD_EXTENSION_LIB_BASE
                    or (
                        CTX_KEY_SPELLING(std_ext_ctx, STD_EXTENSION_LIB_BASE)
                        != CTX_KEY_SPELLING(item_ctx, STD_EXTENSION_LIB_BASE)
                    )
                ){
                    DECLARE_LOCAL (bad);
                    Derelativize(bad, item, VAL_SPECIFIER(loaded_exts));
                    fail(Error_Bad_Extension_Raw(bad));
                }
                else {
                    if (IS_BLANK(CTX_VAR(item_ctx, STD_EXTENSION_LIB_BASE)))
                        continue; //builtin extension
                }

                assert(IS_LIBRARY(CTX_VAR(item_ctx, STD_EXTENSION_LIB_BASE)));

                if (
                    VAL_LIBRARY_FD(CTX_VAR(item_ctx, STD_EXTENSION_LIB_BASE))
                    == VAL_LIBRARY_FD(lib)
                ){
                    // found the existing extension, decrease the reference
                    // added by MAKE_library

                    OS_CLOSE_LIBRARY(VAL_LIBRARY_FD(lib));
                    return KNOWN(item);
                }
            }
        }
        context = Copy_Context_Shallow_Managed(std_ext_ctx);
        Move_Value(CTX_VAR(context, STD_EXTENSION_LIB_BASE), lib);
        Move_Value(CTX_VAR(context, STD_EXTENSION_LIB_FILE), path);

        PUSH_GC_GUARD(context);

        CFUNC *RX_Init = OS_FIND_FUNCTION(VAL_LIBRARY_FD(lib), "RX_Init");
        if (RX_Init == NULL) {
            OS_CLOSE_LIBRARY(VAL_LIBRARY_FD(lib));
            fail(Error_Bad_Extension_Raw(path));
        }

        // Call its RX_Init function for header and code body:
        if (cast(INIT_CFUNC, RX_Init)(CTX_VAR(context, STD_EXTENSION_SCRIPT),
            CTX_VAR(context, STD_EXTENSION_MODULES)) < 0) {
            OS_CLOSE_LIBRARY(VAL_LIBRARY_FD(lib));
            fail(Error_Extension_Init_Raw(path));
        }

        DROP_GC_GUARD(context);
    }
    else {
        assert(IS_HANDLE(ARG(path_or_handle)));
        REBVAL *handle = ARG(path_or_handle);
        if (VAL_HANDLE_CLEANER(handle) != cleanup_extension_init_handler)
            fail(Error_Bad_Extension_Raw(handle));

        INIT_CFUNC RX_Init = cast(INIT_CFUNC, VAL_HANDLE_CFUNC(handle));
        context = Copy_Context_Shallow_Managed(std_ext_ctx);

        PUSH_GC_GUARD(context);

        if (
            RX_Init(
                CTX_VAR(context, STD_EXTENSION_SCRIPT),
                CTX_VAR(context, STD_EXTENSION_MODULES)
            ) < 0
        ){
            fail(Error_Extension_Init_Raw(handle));
        }

        DROP_GC_GUARD(context);
    }

    return Init_Object(D_OUT, context);
}


//
//  unload-extension-helper: native [
//
//  "Unload an extension"
//
//      return: [void!]
//      ext [object!]
//          "The extension to be unloaded"
//      /cleanup
//      cleaner [handle!]
//          "The RX_Quit pointer for the builtin extension"
//  ]
//
REBNATIVE(unload_extension_helper)
{
    INCLUDE_PARAMS_OF_UNLOAD_EXTENSION_HELPER;

    REBCTX *std = VAL_CONTEXT(Get_System(SYS_STANDARD, STD_EXTENSION));
    REBCTX *context = VAL_CONTEXT(ARG(ext));

    if (
        (CTX_LEN(context) <= STD_EXTENSION_LIB_BASE)
        or (
            CTX_KEY_CANON(context, STD_EXTENSION_LIB_BASE)
            != CTX_KEY_CANON(std, STD_EXTENSION_LIB_BASE)
        )
    ){
        fail (Error_Invalid(ARG(ext)));
    }

    int ret;
    if (!REF(cleanup)) {
        REBVAL *lib = CTX_VAR(context, STD_EXTENSION_LIB_BASE);
        if (not IS_LIBRARY(lib))
            fail (Error_Invalid(ARG(ext)));

        if (IS_LIB_CLOSED(VAL_LIBRARY(lib)))
            fail (Error_Bad_Library_Raw());

        QUIT_CFUNC quitter = cast(
            QUIT_CFUNC, OS_FIND_FUNCTION(VAL_LIBRARY_FD(lib), "RX_Quit")
        );

        if (quitter == NULL)
            ret = 0;
        else
            ret = quitter();

        OS_CLOSE_LIBRARY(VAL_LIBRARY_FD(lib));
    }
    else {
        if (VAL_HANDLE_CLEANER(ARG(cleaner)) != cleanup_extension_quit_handler)
            fail (Error_Invalid(ARG(cleaner)));

        QUIT_CFUNC quitter = cast(QUIT_CFUNC, VAL_HANDLE_CFUNC(ARG(cleaner)));
        assert(quitter != NULL);

        ret = quitter();
    }

    if (ret < 0) {
        DECLARE_LOCAL (i);
        Init_Integer(i, ret);
        fail (Error_Fail_To_Quit_Extension_Raw(i));
    }

    return Init_Void(D_OUT);
}


//
// Just an ID for the handler
//
static void cleanup_module_handler(const REBVAL *val)
{
    UNUSED(val);
}


//
//  Make_Extension_Module_Array: C
//
// Make an extension module array for being loaded later
//
REBARR *Make_Extension_Module_Array(
    const REBYTE spec[],
    REBCNT len,
    REBNAT impl[],
    REBCNT n
) {
    // the array will be like [spec C_func]
    REBARR *arr = Make_Arr(2);

    Init_Binary(ARR_AT(arr, 0), Copy_Bytes(spec, len));

    Init_Handle_Managed(
        ARR_AT(arr, 1),
        impl, // It's a *pointer to function pointer*, not a function pointer
        n,
        &cleanup_module_handler
    );

    TERM_ARRAY_LEN(arr, 2);
    return arr;
}


//
//  load-native: native [
//
//  "Load a native from a built-in extension"
//
//      return: "Action created from the native C function"
//          [action!]
//      spec "spec of the native"
//          [block!]
//      cfuncs "a handle returned from RX_Init_ of the extension"
//          [handle!]
//      index "Index of the native"
//          [integer!]
//      /body "Provide a user-equivalent body"
//      code [block!]
//      /unloadable "Can be unloaded later (when extension is unloaded)"
//  ]
//
REBNATIVE(load_native)
{
    INCLUDE_PARAMS_OF_LOAD_NATIVE;

    if (VAL_HANDLE_CLEANER(ARG(cfuncs)) != cleanup_module_handler)
        fail ("HANDLE! passed to LOAD-NATIVE did not come from RX_Init");

    REBI64 index = VAL_INT64(ARG(index));
    if (index < 0 or cast(uintptr_t, index) >= VAL_HANDLE_LEN(ARG(cfuncs)))
        fail ("Index of native is outside range specified by RX_Init");

    REBNAT dispatcher = VAL_HANDLE_POINTER(REBNAT, ARG(cfuncs))[index];
    REBACT *native = Make_Action(
        Make_Paramlist_Managed_May_Fail(
            ARG(spec),
            MKF_KEYWORDS | MKF_FAKE_RETURN
        ),
        dispatcher, // unique
        NULL, // no facade (use paramlist)
        NULL, // no specialization exemplar (or inherited exemplar)
        IDX_NATIVE_MAX // details array capacity
    );

    SET_VAL_FLAG(ACT_ARCHETYPE(native), ACTION_FLAG_NATIVE);
    if (REF(unloadable))
        SET_VAL_FLAG(ACT_ARCHETYPE(native), ACTION_FLAG_UNLOADABLE_NATIVE);

    REBARR *details = ACT_DETAILS(native);

    if (REF(body))
        Move_Value(ARR_AT(details, IDX_NATIVE_BODY), ARG(code));
    else
        Init_Blank(ARR_AT(details, IDX_NATIVE_BODY)); // no body provided

    // !!! Ultimately extensions should all have associated modules known
    // about here.  That should be where rebXXX() APIs do their binding, and
    // they should be isolated.  For the moment, use the lib context.
    // This runs risk of contamination if they aren't careful...but it should
    // insulate the modules from user context changes.  (Note that R3-Alpha
    // modules would bind modules direct to lib w/o the Isolate option...)
    //
    Init_Object(ARR_AT(details, IDX_NATIVE_CONTEXT), Lib_Context);

    return Init_Action_Unbound(D_OUT, native);
}


//
//  Unloaded_Dispatcher: C
//
// This will be the dispatcher for the natives in an extension after the
// extension is unloaded.
//
static const REBVAL *Unloaded_Dispatcher(REBFRM *f)
{
    UNUSED(f);

    fail (Error_Native_Unloaded_Raw(ACT_ARCHETYPE(FRM_PHASE(f))));
}


//
//  unload-native: native [
//
//  "Unload a native when the containing extension is unloaded"
//
//      return: [void!]
//      native "The native function to be unloaded"
//          [action!]
//      /relax "Don't error if it's not actually unloadable (REVIEW!)"
//  ]
//
REBNATIVE(unload_native)
{
    INCLUDE_PARAMS_OF_UNLOAD_NATIVE;

    REBACT *action = VAL_ACTION(ARG(native));
    if (not GET_ACT_FLAG(action, ACTION_FLAG_UNLOADABLE_NATIVE)) {
        if (REF(relax)) {
            //
            // !!! Under the "OneAction" policy, there is no usermode visible
            // way to distinguish an unloadable native from a user function.
            // There could be a property written onto functions with META-OF,
            // but the system doesn't prohibit users from tweaking that, so
            // it isn't currently reliable.
            //
            // In this case, it should probably be rethought to where the
            // loading process remembers a list of natives it loaded, instead
            // of trying to unload every function.  But as a general premise,
            // even though the evaluator only cares about one ACTION! as an
            // interface, things like SOURCE or UNLOAD-NATIVE will have
            // specific interactions with sublcasses based on the dispatcher.
            // For minimal invasiveness right now, the /RELAX refinement just
            // documents the issue in the unloading process.
            //
            return Init_Void(D_OUT);
        }

        fail (Error_Non_Unloadable_Native_Raw(ARG(native)));
    }

    ACT_DISPATCHER(action) = &Unloaded_Dispatcher;
    return Init_Void(D_OUT);
}


//
//  Hook_Datatype: C
//
// Poor-man's user-defined type hack: this really just gives the ability to
// have the only thing the core knows about a "user-defined-type" be its
// value cell structure and datatype enum number...but have the behaviors
// come from functions that are optionally registered in an extension.
//
// (Actual facets of user-defined types will ultimately be dispatched through
// Rebol-frame-interfaced functions, not raw C structures like this.)
//
void Hook_Datatype(
    enum Reb_Kind kind,
    REBTAF taf,
    REBPEF pef,
    REBCTF ctf,
    MAKE_CFUNC make_func,
    TO_CFUNC to_func,
    MOLD_CFUNC mold_func
) {
    if (Value_Dispatch[kind] != &T_Unhooked)
        fail ("Value_Dispatch already hooked.");
    if (Path_Dispatch[kind] != &PD_Unhooked)
        fail ("Path_Dispatch already hooked.");
    if (Compare_Types[kind] != &CT_Unhooked)
        fail ("Compare_Types already hooked.");
    if (Make_Dispatch[kind] != &MAKE_Unhooked)
        fail ("Make_Dispatch already hooked.");
    if (To_Dispatch[kind] != &TO_Unhooked)
        fail ("To_Dispatch already hooked.");
    if (Mold_Or_Form_Dispatch[kind] != &MF_Unhooked)
        fail ("Mold_Or_Form_Dispatch already hooked.");

    Value_Dispatch[kind] = taf;
    Path_Dispatch[kind] = pef;
    Compare_Types[kind] = ctf;
    Make_Dispatch[kind] = make_func;
    To_Dispatch[kind] = to_func;
    Mold_Or_Form_Dispatch[kind] = mold_func;
}


//
//  Unhook_Datatype: C
//
void Unhook_Datatype(enum Reb_Kind kind)
{
    if (Value_Dispatch[kind] == &T_Unhooked)
        fail ("Value_Dispatch is not hooked.");
    if (Path_Dispatch[kind] == &PD_Unhooked)
        fail ("Path_Dispatch is not hooked.");
    if (Compare_Types[kind] == &CT_Unhooked)
        fail ("Compare_Types is not hooked.");
    if (Make_Dispatch[kind] == &MAKE_Unhooked)
        fail ("Make_Dispatch is not hooked.");
    if (To_Dispatch[kind] == &TO_Unhooked)
        fail ("To_Dispatch is not hooked.");
    if (Mold_Or_Form_Dispatch[kind] == &MF_Unhooked)
        fail ("Mold_Or_Form_Dispatch is not hooked.");

    Value_Dispatch[kind] = &T_Unhooked;
    Path_Dispatch[kind] = &PD_Unhooked;
    Compare_Types[kind] = &CT_Unhooked;
    Make_Dispatch[kind] = &MAKE_Unhooked;
    To_Dispatch[kind] = &TO_Unhooked;
    Mold_Or_Form_Dispatch[kind] = &MF_Unhooked;
}
