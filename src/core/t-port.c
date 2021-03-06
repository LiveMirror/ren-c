//
//  File: %t-port.c
//  Summary: "port datatype"
//  Section: datatypes
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

#include "sys-core.h"


//
//  CT_Port: C
//
REBINT CT_Port(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode < 0) return -1;
    return VAL_CONTEXT(a) == VAL_CONTEXT(b);
}


//
//  MAKE_Port: C
//
// Create a new port. This is done by calling the MAKE_PORT
// function stored in the system/intrinsic object.
//
void MAKE_Port(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_PORT);
    UNUSED(kind);

    const bool fully = true; // error if not all arguments consumed

    REBVAL *make_port_helper = CTX_VAR(Sys_Context, SYS_CTX_MAKE_PORT_P);
    assert(IS_ACTION(make_port_helper));

    assert(not IS_NULLED(arg)); // would need to DEVOID it otherwise
    if (Apply_Only_Throws(out, fully, make_port_helper, arg, rebEND))
        fail (Error_No_Catch_For_Throw(out));

    // !!! Shouldn't this be testing for !IS_PORT( ) ?
    if (IS_BLANK(out))
        fail (Error_Invalid_Spec_Raw(arg));
}


//
//  TO_Port: C
//
void TO_Port(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_PORT);
    UNUSED(kind);

    if (!IS_OBJECT(arg))
        fail (Error_Bad_Make(REB_PORT, arg));

    // !!! cannot convert TO a PORT! without copying the whole context...
    // which raises the question of why convert an object to a port,
    // vs. making it as a port to begin with (?)  Look into why
    // system/standard/port is made with CONTEXT and not with MAKE PORT!
    //
    REBCTX *context = Copy_Context_Shallow_Managed(VAL_CONTEXT(arg));
    RESET_VAL_HEADER(CTX_ARCHETYPE(context), REB_PORT);
    Init_Port(out, context);
}


//
//  Retrigger_Append_As_Write: C
//
// !!! In R3-Alpha, for the convenience of being able to APPEND to something
// that may be a FILE!-based PORT! or a BINARY! or STRING! with a unified
// interface, the APPEND command was re-interpreted as a WRITE/APPEND.  But
// it was done with presumption that APPEND and WRITE had compatible frames,
// which generally speaking they do not.
//
// This moves the functionality to an actual retriggering which calls whatever
// WRITE/APPEND would do in a generic fashion with a new frame.  Not all
// ports do this, as some have their own interpretation of APPEND.  It's
// hacky, but still not as bad as it was.  Review.
//
const REBVAL *Retrigger_Append_As_Write(REBFRM *frame_) {
    INCLUDE_PARAMS_OF_APPEND;

    // !!! Something like `write/append %foo.txt "data"` knows to convert
    // %foo.txt to a port before trying the write, but if you say
    // `append %foo.txt "data"` you get `%foo.txtdata`.  Some actions are like
    // this, e.g. PICK, where they can't do the automatic conversion.
    //
    assert(IS_PORT(ARG(series))); // !!! poorly named
    UNUSED(ARG(series));
    if (not (
        IS_BINARY(ARG(value))
        or IS_TEXT(ARG(value))
        or IS_BLOCK(ARG(value)))
    ){
        fail (Error_Invalid(ARG(value)));
    }

    if (REF(part)) {
        UNUSED(ARG(limit));
        fail (Error_Bad_Refines_Raw());
    }
    if (REF(only))
        fail (Error_Bad_Refines_Raw());
    if (REF(dup)) {
        UNUSED(ARG(count));
        fail (Error_Bad_Refines_Raw());
    }
    if (REF(line))
        fail (Error_Bad_Refines_Raw());

    return rebRun("write/append", D_ARG(1), D_ARG(2), rebEND);
}


//
//  REBTYPE: C
//
// !!! The concept of port dispatch from R3-Alpha is that it delegates to a
// handler which may be native code or user code.
//
REBTYPE(Port)
{
    // !!! The ability to transform some BLOCK!s into PORT!s for some actions
    // was hardcoded in a fairly ad-hoc way in R3-Alpha, which was based on
    // an integer range of action numbers.  Ren-C turned these numbers into
    // symbols, where order no longer applied.  The mechanism needs to be
    // rethought, see:
    //
    // https://github.com/metaeducation/ren-c/issues/311
    //
    if (not IS_PORT(D_ARG(1))) {
        switch (VAL_WORD_SYM(verb)) {

        case SYM_READ:
        case SYM_WRITE:
        case SYM_QUERY:
        case SYM_OPEN:
        case SYM_CREATE:
        case SYM_DELETE:
        case SYM_RENAME: {
            //
            // !!! We are going to "re-apply" the call frame with routines we
            // are going to read the D_ARG(1) slot *implicitly* regardless of
            // what value points to.
            //
            MAKE_Port(D_OUT, REB_PORT, D_ARG(1));
            Move_Value(D_ARG(1), D_OUT);
            break; }

        case SYM_ON_WAKE_UP:
            break;

        // Once handled SYM_REFLECT here by delegating to T_Context(), but
        // common reflectors now in Context_Common_Action_Or_End()

        default:
            break;
        }
    }

    if (not IS_PORT(D_ARG(1)))
        fail (Error_Illegal_Action(VAL_TYPE(D_ARG(1)), verb));

    REBVAL *port = D_ARG(1);

    const REBVAL *r = Context_Common_Action_Maybe_Unhandled(frame_, verb);
    if (r != R_UNHANDLED)
        return r;

    return Do_Port_Action(frame_, port, verb);
}
