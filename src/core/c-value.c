//
//  File: %c-value.c
//  Summary: "Generic REBVAL Support Services and Debug Routines"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
// These are mostly DEBUG-build routines to support the macros and definitions
// in %sys-value.h.
//
// These are not specific to any given type.  For the type-specific REBVAL
// code, see files with names like %t-word.c, %t-logic.c, %t-integer.c...
//

#include "sys-core.h"


#if !defined(NDEBUG)

//
//  Panic_Value_Debug: C
//
// This is a debug-only "error generator", which will hunt through all the
// series allocations and panic on the series that contains the value (if
// it can find it).  This will allow those using Address Sanitizer or
// Valgrind to know a bit more about where the value came from.
//
// Additionally, if it happens to be a void or trash, LOGIC!, BAR!, or NONE!
// it will dump out where the initialization happened if that information
// was stored.
//
ATTRIBUTE_NO_RETURN void Panic_Value_Debug(const RELVAL *v) {
    fflush(stdout);
    fflush(stderr);

    REBNOD *containing = Try_Find_Containing_Node_Debug(v);

    switch (VAL_TYPE_RAW(v)) {
    case REB_MAX_VOID:
    case REB_BLANK:
    case REB_LOGIC:
    case REB_BAR:
      #if defined(DEBUG_TRACK_CELLS)
        printf("REBVAL init ");

        #if defined(DEBUG_TRACK_EXTEND_CELLS)
            #if defined(DEBUG_COUNT_TICKS)
                printf("@ tick #%d", cast(unsigned int, v->tick));
                if (v->move_tick != 0)
                    printf("moved @ #%d", cast(unsigned int, v->move_tick));
            #endif

            printf("@ %s:%d\n", v->track.file, v->track.line);
        #else
            #if defined(DEBUG_COUNT_TICKS)
                printf("@ tick #%d", cast(unsigned int, v->extra.tick));
            #endif

            printf("@ %s:%d\n", v->payload.track.file, v->payload.track.line);
        #endif
      #else
        printf("No track info (see DEBUG_TRACK_CELLS/DEBUG_COUNT_TICKS)\n");
      #endif
        fflush(stdout);
        break;

    default:
        break;
    }

    printf("Kind=%d\n", cast(int, VAL_TYPE_RAW(v)));
    fflush(stdout);

    if (containing != NULL and NOT_CELL(containing)) {
        printf("Containing series for value pointer found, panicking it:\n");
        Panic_Series_Debug(SER(containing));
    }

    if (containing != NULL) {
        printf("Containing pairing for value pointer found, panicking it:\n");
        Panic_Series_Debug(cast(REBSER*, containing)); // won't pass SER()
    }

    printf("No containing series for value...panicking to make stack dump:\n");
    Panic_Series_Debug(SER(EMPTY_ARRAY));
}


//
//  VAL_SPECIFIC_Debug: C
//
REBCTX *VAL_SPECIFIC_Debug(const REBVAL *v)
{
    assert(
        VAL_TYPE(v) == REB_0_REFERENCE
        or ANY_WORD(v)
        or ANY_ARRAY(v)
        or IS_VARARGS(v)
        or IS_ACTION(v)
        or ANY_CONTEXT(v)
    );

    REBCTX *specific = VAL_SPECIFIC_COMMON(v);

    if (SPC(specific) != SPECIFIED) {
        //
        // Basic sanity check: make sure it's a context at all
        //
        if (NOT_SER_FLAG(CTX_VARLIST(specific), ARRAY_FLAG_VARLIST)) {
            printf("Non-CONTEXT found as specifier in specific value\n");
            panic (specific); // may not be a series, either
        }

        // While an ANY-WORD! can be bound specifically to an arbitrary
        // object, an ANY-ARRAY! only becomes bound specifically to frames.
        // The keylist for a frame's context should come from a function's
        // paramlist, which should have an ACTION! value in keylist[0]
        //
        if (ANY_ARRAY(v))
            assert(IS_ACTION(CTX_ROOTKEY(specific)));
    }

    return specific;
}


#ifdef CPLUSPLUS_11
//
// This destructor checks to make sure that any cell that was created via
// DECLARE_LOCAL got properly initialized.
//
Reb_Specific_Value::~Reb_Specific_Value ()
{
    assert(header.bits & NODE_FLAG_CELL);

    enum Reb_Kind kind = VAL_TYPE_RAW(this);
    assert(
        header.bits & NODE_FLAG_FREE
            ? kind == REB_MAX_PLUS_ONE_TRASH
            : kind <= REB_MAX_VOID
    );
}
#endif

#endif // !defined(NDEBUG)


#ifdef DEBUG_HAS_PROBE

inline static void Probe_Print_Helper(
    const void *p,
    const char *label,
    const char *file,
    int line
){
    printf("\n**PROBE(%s, %p): ", label, p);
  #ifdef DEBUG_COUNT_TICKS
    printf("tick %d ", cast(int, TG_Tick));
  #endif
    printf("%s:%d\n", file, line);

    fflush(stdout);
    fflush(stderr);
}


inline static void Probe_Molded_Value(const REBVAL *v)
{
    DECLARE_MOLD (mo);
    Push_Mold(mo);
    Mold_Value(mo, v);

    printf("%s\n", s_cast(BIN_AT(mo->series, mo->start)));
    fflush(stdout);

    Drop_Mold(mo);
}


//
//  Probe_Core_Debug: C
//
// Use PROBE() to invoke, see notes there.
//
void* Probe_Core_Debug(
    const void *p,
    const char *file,
    int line
){
    DECLARE_MOLD (mo);
    Push_Mold(mo);

    REBOOL was_disabled = GC_Disabled;
    GC_Disabled = TRUE;

    switch (Detect_Rebol_Pointer(p)) {
    case DETECTED_AS_NULL:
        Probe_Print_Helper(p, "C nullptr", file, line);
        break;

    case DETECTED_AS_UTF8:
        Probe_Print_Helper(p, "C String", file, line);
        printf("\"%s\"\n", cast(const char*, p));
        break;

    case DETECTED_AS_SERIES: {
        REBSER *s = m_cast(REBSER*, cast(const REBSER*, p));

        ASSERT_SERIES(s); // if corrupt, gives better info than a print crash

        if (GET_SER_FLAG(s, ARRAY_FLAG_VARLIST)) {
            Probe_Print_Helper(p, "Context Varlist", file, line);
            Probe_Molded_Value(CTX_ARCHETYPE(CTX(s)));
        }
        else {
            // This routine is also a little catalog of the outlying series
            // types in terms of sizing, just to know what they are.

            if (SER_WIDE(s) == sizeof(REBYTE)) {
                Probe_Print_Helper(p, "Byte-Size Series", file, line);

                // !!! Duplication of code in MF_Binary
                //
                const REBOOL brk = (BIN_LEN(s) > 32);
                REBSER *enbased = Encode_Base16(BIN_HEAD(s), BIN_LEN(s), brk);
                Append_Unencoded(mo->series, "#{");
                Append_Utf8_Utf8(
                    mo->series,
                    cs_cast(BIN_HEAD(enbased)), BIN_LEN(enbased)
                );
                Append_Unencoded(mo->series, "}");
                Free_Unmanaged_Series(enbased);
            }
            else if (SER_WIDE(s) == sizeof(REBUNI)) {
                Probe_Print_Helper(p, "REBWCHAR-Size Series", file, line);
                Mold_Text_Series_At(mo, s, 0); // not necessarily TEXT!
            }
            else if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY)) {
                Probe_Print_Helper(p, "Array", file, line);
                Mold_Array_At(mo, ARR(s), 0, "[]"); // not necessarily BLOCK!
            }
            else if (s == PG_Canons_By_Hash) {
                printf("can't probe PG_Canons_By_Hash (TBD: add probing)\n");
                panic (s);
            }
            else if (s == GC_Guarded) {
                printf("can't probe GC_Guarded (TBD: add probing)\n");
                panic (s);
            }
            else
                panic (s);

        }
        break; }

    case DETECTED_AS_FREED_SERIES:
        Probe_Print_Helper(p, "Freed Series", file, line);
        panic (p);

    case DETECTED_AS_VALUE: {
        Probe_Print_Helper(p, "Value", file, line);
        Mold_Value(mo, cast(const REBVAL*, p));
        break; }

    case DETECTED_AS_END:
        Probe_Print_Helper(p, "END", file, line);
        break;

    case DETECTED_AS_TRASH_CELL:
        Probe_Print_Helper(p, "Trash Cell", file, line);
        panic (p);
    }

    if (mo->start != SER_LEN(mo->series))
        printf("%s\n", s_cast(BIN_AT(mo->series, mo->start)));
    fflush(stdout);

    Drop_Mold(mo);

    assert(GC_Disabled == TRUE);
    GC_Disabled = was_disabled;

    return m_cast(void*, p); // must be cast back to const if source was const
}

#endif // defined(DEBUG_HAS_PROBE)
