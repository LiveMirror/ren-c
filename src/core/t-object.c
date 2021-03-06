//
//  File: %t-object.c
//  Summary: "object datatype"
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



static bool Equal_Context(const RELVAL *val, const RELVAL *arg)
{
    REBCTX *f1;
    REBCTX *f2;
    REBVAL *key1;
    REBVAL *key2;
    REBVAL *var1;
    REBVAL *var2;

    // ERROR! and OBJECT! may both be contexts, for instance, but they will
    // not compare equal just because their keys and fields are equal
    //
    if (VAL_TYPE(arg) != VAL_TYPE(val))
        return false;

    f1 = VAL_CONTEXT(val);
    f2 = VAL_CONTEXT(arg);

    // Short circuit equality: `same?` objects always equal
    //
    if (f1 == f2)
        return true;

    // We can't short circuit on unequal frame lengths alone, because hidden
    // fields of objects (notably `self`) do not figure into the `equal?`
    // of their public portions.

    key1 = CTX_KEYS_HEAD(f1);
    key2 = CTX_KEYS_HEAD(f2);
    var1 = CTX_VARS_HEAD(f1);
    var2 = CTX_VARS_HEAD(f2);

    // Compare each entry, in order.  This order dependence suggests that
    // an object made with `make object! [[a b][a: 1 b: 2]]` will not be equal
    // to `make object! [[b a][b: 1 a: 2]]`.  Although Rebol does not allow
    // positional picking out of objects, it does allow positional setting
    // currently (which it likely should not), hence they are functionally
    // distinct for now.  Yet those two should probably be `equal?`.
    //
    for (; NOT_END(key1) && NOT_END(key2); key1++, key2++, var1++, var2++) {
    no_advance:
        //
        // Hidden vars shouldn't affect the comparison.
        //
        if (Is_Param_Hidden(key1)) {
            key1++; var1++;
            if (IS_END(key1)) break;
            goto no_advance;
        }
        if (Is_Param_Hidden(key2)) {
            key2++; var2++;
            if (IS_END(key2)) break;
            goto no_advance;
        }

        // Do ordinary comparison of the typesets
        //
        if (Cmp_Value(key1, key2, false) != 0)
            return false;

        // The typesets contain a symbol as well which must match for
        // objects to consider themselves to be equal (but which do not
        // count in comparison of the typesets)
        //
        if (VAL_KEY_CANON(key1) != VAL_KEY_CANON(key2))
            return false;

        // !!! A comment here said "Use Compare_Modify_Values();"...but it
        // doesn't... it calls Cmp_Value (?)
        //
        if (Cmp_Value(var1, var2, false) != 0)
            return false;
    }

    // Either key1 or key2 is at the end here, but the other might contain
    // all hidden values.  Which is okay.  But if a value isn't hidden,
    // they don't line up.
    //
    for (; NOT_END(key1); key1++, var1++) {
        if (not Is_Param_Hidden(key1))
            return false;
    }
    for (; NOT_END(key2); key2++, var2++) {
        if (not Is_Param_Hidden(key2))
            return false;
    }

    return true;
}


static void Append_To_Context(REBCTX *context, REBVAL *arg)
{
    // Can be a word:
    if (ANY_WORD(arg)) {
        if (0 == Find_Canon_In_Context(context, VAL_WORD_CANON(arg), true)) {
            Expand_Context(context, 1); // copy word table also
            Append_Context(context, 0, VAL_WORD_SPELLING(arg));
            // default of Append_Context is that arg's value is void
        }
        return;
    }

    if (not IS_BLOCK(arg))
        fail (Error_Invalid(arg));

    // Process word/value argument block:

    RELVAL *item = VAL_ARRAY_AT(arg);

    // Can't actually fail() during a collect, so make sure any errors are
    // set and then jump to a Collect_End()
    //
    REBCTX *error = NULL;

    struct Reb_Collector collector;
    Collect_Start(&collector, COLLECT_ANY_WORD | COLLECT_AS_TYPESET);

    // Leave the [0] slot blank while collecting (ROOTKEY/ROOTPARAM), but
    // valid (but "unreadable") bits so that the copy will still work.
    //
    Init_Unreadable_Blank(ARR_HEAD(BUF_COLLECT));
    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, 1);

    // Setup binding table with obj words.  Binding table is empty so don't
    // bother checking for duplicates.
    //
    Collect_Context_Keys(&collector, context, false);

    // Examine word/value argument block

    RELVAL *word;
    for (word = item; NOT_END(word); word += 2) {
        if (!IS_WORD(word) && !IS_SET_WORD(word)) {
            error = Error_Invalid_Core(word, VAL_SPECIFIER(arg));
            goto collect_end;
        }

        REBSTR *canon = VAL_WORD_CANON(word);

        if (Try_Add_Binder_Index(
            &collector.binder, canon, ARR_LEN(BUF_COLLECT))
        ){
            //
            // Wasn't already collected...so we added it...
            //
            EXPAND_SERIES_TAIL(SER(BUF_COLLECT), 1);
            Init_Typeset(
                ARR_LAST(BUF_COLLECT),
                TS_VALUE, // !!! Currently ignored
                VAL_WORD_SPELLING(word)
            );
        }
        if (IS_END(word + 1))
            break; // fix bug#708
    }

    TERM_ARRAY_LEN(BUF_COLLECT, ARR_LEN(BUF_COLLECT));

    // Append new words to obj
    //
    REBCNT len; // goto crosses initialization
    len = CTX_LEN(context) + 1;
    Expand_Context(context, ARR_LEN(BUF_COLLECT) - len);

    RELVAL *collect_key;
    for (
        collect_key = ARR_AT(BUF_COLLECT, len);
        NOT_END(collect_key);
        ++collect_key
    ){
        assert(IS_TYPESET(collect_key));
        Append_Context(context, NULL, VAL_KEY_SPELLING(collect_key));
    }

    // Set new values to obj words
    for (word = item; NOT_END(word); word += 2) {
        REBCNT i = Get_Binder_Index_Else_0(
            &collector.binder, VAL_WORD_CANON(word)
        );
        assert(i != 0);

        REBVAL *key = CTX_KEY(context, i);
        REBVAL *var = CTX_VAR(context, i);

        if (GET_VAL_FLAG(var, CELL_FLAG_PROTECTED)) {
            error = Error_Protected_Key(key);
            goto collect_end;
        }

        if (Is_Param_Hidden(key)) {
            error = Error_Hidden_Raw();
            goto collect_end;
        }

        if (IS_END(word + 1)) {
            Init_Blank(var);
            break; // fix bug#708
        }
        else {
            assert(NOT_VAL_FLAG(&word[1], VALUE_FLAG_ENFIXED));
            Derelativize(var, &word[1], VAL_SPECIFIER(arg));
        }
    }

collect_end:
    Collect_End(&collector);

    if (error != NULL)
        fail (error);
}


//
//  CT_Context: C
//
REBINT CT_Context(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode < 0) return -1;
    return Equal_Context(a, b) ? 1 : 0;
}


//
//  MAKE_Context: C
//
// !!! MAKE functions currently don't have an explicit protocol for
// thrown values.  So out just might be set as thrown.  Review.
//
void MAKE_Context(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (kind == REB_FRAME) {
        //
        // !!! The feature of MAKE FRAME! from a VARARGS! would be interesting
        // as a way to support usermode authoring of things like MATCH.
        // For now just support ACTION! (or path/word to specify an action)
        //
        REBDSP lowest_ordered_dsp = DSP;

        REBSTR *opt_label;
        if (Get_If_Word_Or_Path_Throws(
            out,
            &opt_label,
            arg,
            SPECIFIED,
            true // push_refinements, don't specialize ACTION! if PATH!
        )){
            return; // !!! no explicit Throws() protocol, review
        }

        if (not IS_ACTION(out))
            fail (Error_Bad_Make(kind, arg));

        REBCTX *exemplar = Make_Context_For_Action(
            out, // being used here as input (e.g. the ACTION!)
            lowest_ordered_dsp, // will weave in the refinements pushed
            nullptr // no binder needed, not running any code
        );

        // See notes in %c-specialize.c about the special encoding used to
        // put /REFINEMENTs in refinement slots (instead of true/false/null)
        // to preserve the order of execution.
        //
        Init_Frame(out, exemplar);
        return;
    }

    if (kind == REB_OBJECT && IS_BLOCK(arg)) {
        //
        // Simple object creation with no evaluation, so all values are
        // handled "as-is".  Should have a spec block and a body block.
        //
        // Note: In %r3-legacy.r, the old evaluative MAKE OBJECT! is
        // done by redefining MAKE itself, and calling the CONSTRUCT
        // generator if the make def is not the [[spec][body]] format.

        if (
            VAL_LEN_AT(arg) != 2
            || !IS_BLOCK(VAL_ARRAY_AT(arg)) // spec
            || !IS_BLOCK(VAL_ARRAY_AT(arg) + 1) // body
        ) {
            fail (Error_Bad_Make(kind, arg));
        }

        // !!! Spec block is currently ignored, but required.

        Init_Object(
            out,
            Construct_Context_Managed(
                REB_OBJECT,
                VAL_ARRAY_AT(VAL_ARRAY_AT(arg) + 1),
                VAL_SPECIFIER(arg),
                NULL // no parent
            )
        );

        return;
    }

    // make error! [....]
    //
    // arg is block/string, but let Make_Error_Object_Throws do the
    // type checking.
    //
    if (kind == REB_ERROR) {
        if (Make_Error_Object_Throws(out, arg))
            return; // !!! no explicit Throws() protocol, review

        return;
    }

    // `make object! 10` - currently not prohibited for any context type
    //
    if (ANY_NUMBER(arg)) {
        //
        // !!! Temporary!  Ultimately SELF will be a user protocol.
        // We use Make_Selfish_Context while MAKE is filling in for
        // what will be responsibility of the generators, just to
        // get "completely fake SELF" out of index slot [0]
        //
        REBCTX *context = Make_Selfish_Context_Detect_Managed(
            kind, // type
            END_NODE, // values to scan for toplevel set-words (empty)
            NULL // parent
        );

        // !!! Allocation when SELF is not the responsibility of MAKE
        // will be more basic and look like this.
        //
        /*
        REBINT n = Int32s(arg, 0);
        context = Alloc_Context(kind, n);
        RESET_VAL_HEADER(CTX_ARCHETYPE(context), target);
        CTX_SPEC(context) = NULL;
        CTX_BODY(context) = NULL; */
        Init_Any_Context(out, kind, context);

        return;
    }

    // make object! map!
    if (IS_MAP(arg)) {
        REBCTX *context = Alloc_Context_From_Map(VAL_MAP(arg));
        Init_Any_Context(out, kind, context);
        return;
    }

    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Context: C
//
void TO_Context(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (kind == REB_ERROR) {
        //
        // arg is checked to be block or string
        //
        if (Make_Error_Object_Throws(out, arg))
            fail (Error_No_Catch_For_Throw(out));

        return;
    }

    if (kind == REB_OBJECT) {
        //
        // !!! Contexts hold canon values now that are typed, this init
        // will assert--a TO conversion would thus need to copy the varlist
        //
        Init_Object(out, VAL_CONTEXT(arg));
        return;
    }

    fail (Error_Bad_Make(kind, arg));
}


//
//  PD_Context: C
//
const REBVAL *PD_Context(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    REBCTX *c = VAL_CONTEXT(pvs->out);

    if (not IS_WORD(picker))
        return R_UNHANDLED;

    const bool always = false;
    REBCNT n = Find_Canon_In_Context(c, VAL_WORD_CANON(picker), always);

    if (n == 0)
        return nullptr;

    if (opt_setval) {
        FAIL_IF_READ_ONLY_CONTEXT(c);

        if (GET_VAL_FLAG(CTX_VAR(c, n), CELL_FLAG_PROTECTED))
            fail (Error_Protected_Word_Raw(picker));
    }

    pvs->u.ref.cell = CTX_VAR(c, n);
    pvs->u.ref.specifier = SPECIFIED;
    return R_REFERENCE;
}


//
//  meta-of: native [
//
//  {Get a reference to the "meta" context associated with a value.}
//
//      return: [<opt> any-context!]
//      value [blank! action! any-context!]
//  ]
//
REBNATIVE(meta_of)
//
// See notes accompanying the `meta` field in the REBSER definition.
{
    INCLUDE_PARAMS_OF_META_OF;

    REBVAL *v = ARG(value);
    if (IS_BLANK(v))
        return nullptr;

    REBCTX *meta;
    if (IS_ACTION(v))
        meta = VAL_ACT_META(v);
    else {
        assert(ANY_CONTEXT(v));
        meta = MISC(VAL_CONTEXT(v)).meta;
    }

    if (not meta)
        return nullptr;

    RETURN (CTX_ARCHETYPE(meta));
}


//
//  set-meta: native [
//
//  {Set "meta" object associated with all references to a value.}
//
//      return: [<opt> any-context!]
//      value [action! any-context!]
//      meta [<opt> any-context!]
//  ]
//
REBNATIVE(set_meta)
//
// See notes accompanying the `meta` field in the REBSER definition.
{
    INCLUDE_PARAMS_OF_SET_META;

    REBCTX *meta;
    if (ANY_CONTEXT(ARG(meta))) {
        if (VAL_BINDING(ARG(meta)) != UNBOUND)
            fail ("SET-META can't store context bindings, must be unbound");

        meta = VAL_CONTEXT(ARG(meta));
    }
    else {
        assert(IS_NULLED(ARG(meta)));
        meta = nullptr;
    }

    REBVAL *v = ARG(value);

    if (IS_ACTION(v))
        MISC(VAL_ACT_PARAMLIST(v)).meta = meta;
    else {
        assert(ANY_CONTEXT(v));
        MISC(VAL_CONTEXT(v)).meta = meta;
    }

    if (not meta)
        return nullptr;

    RETURN (CTX_ARCHETYPE(meta));
}


//
//  Copy_Context_Core_Managed: C
//
// Copying a generic context is not as simple as getting the original varlist
// and duplicating that.  For instance, a "live" FRAME! context (e.g. one
// which is created by a function call on the stack) has to have its "vars"
// (the args and locals) copied from the chunk stack.  Several other things
// have to be touched up to ensure consistency of the rootval and the
// relevant ->link and ->misc fields in the series node.
//
REBCTX *Copy_Context_Core_Managed(REBCTX *original, REBU64 types)
{
    assert(NOT_SER_INFO(original, SERIES_INFO_INACCESSIBLE));

    REBARR *varlist = Make_Arr_For_Copy(
        CTX_LEN(original) + 1,
        SERIES_MASK_CONTEXT | NODE_FLAG_MANAGED,
        nullptr // original_array, N/A because LINK()/MISC() used otherwise
    );
    REBVAL *dest = KNOWN(ARR_HEAD(varlist)); // all context vars are SPECIFIED

    // The type information and fields in the rootvar (at head of the varlist)
    // get filled in with a copy, but the varlist needs to be updated in the
    // copied rootvar to the one just created.
    //
    Move_Value(dest, CTX_ARCHETYPE(original));
    dest->payload.any_context.varlist = varlist;

    ++dest;

    // Now copy the actual vars in the context, from wherever they may be
    // (might be in an array, or might be in the chunk stack for FRAME!)
    //
    REBVAL *src = CTX_VARS_HEAD(original);
    for (; NOT_END(src); ++src, ++dest)
        Move_Var(dest, src); // keep VALUE_FLAG_ENFIXED, ARG_MARKED_CHECKED

    TERM_ARRAY_LEN(varlist, CTX_LEN(original) + 1);
    SET_SER_FLAGS(varlist, SERIES_MASK_CONTEXT);

    REBCTX *copy = CTX(varlist); // now a well-formed context

    // Reuse the keylist of the original.  (If the context of the source or
    // the copy are expanded, the sharing is unlinked and a copy is made).
    // This goes into the ->link field of the REBSER node.
    //
    INIT_CTX_KEYLIST_SHARED(copy, CTX_KEYLIST(original));

    // A FRAME! in particular needs to know if it points back to a stack
    // frame.  The pointer is NULLed out when the stack level completes.
    // If we're copying a frame here, we know it's not running.
    //
    if (CTX_TYPE(original) == REB_FRAME)
        MISC(varlist).meta = NULL;
    else {
        // !!! Should the meta object be copied for other context types?
        // Deep copy?  Shallow copy?  Just a reference to the same object?
        //
        MISC(varlist).meta = NULL;
    }

    if (types != 0) {
        Clonify_Values_Len_Managed(
            CTX_VARS_HEAD(copy),
            SPECIFIED,
            CTX_LEN(copy),
            types
        );
    }

    return copy;
}


//
//  MF_Context: C
//
void MF_Context(REB_MOLD *mo, const RELVAL *v, bool form)
{
    REBSER *out = mo->series;

    REBCTX *c = VAL_CONTEXT(v);

    // Prevent endless mold loop:
    //
    if (Find_Pointer_In_Series(TG_Mold_Stack, c) != NOT_FOUND) {
        if (not form) {
            Pre_Mold(mo, v); // If molding, get #[object! etc.
            Append_Utf8_Codepoint(out, '[');
        }
        Append_Unencoded(out, "...");

        if (not form) {
            Append_Utf8_Codepoint(out, ']');
            End_Mold(mo);
        }
        return;
    }
    Push_Pointer_To_Series(TG_Mold_Stack, c);

    if (form) {
        //
        // Mold all words and their values:
        //
        REBVAL *key = CTX_KEYS_HEAD(c);
        REBVAL *var = CTX_VARS_HEAD(c);
        bool had_output = false;
        for (; NOT_END(key); key++, var++) {
            if (not Is_Param_Hidden(key)) {
                had_output = true;
                Emit(mo, "N: V\n", VAL_KEY_SPELLING(key), var);
            }
        }

        // Remove the final newline...but only if WE added to the buffer
        //
        if (had_output) {
            SET_SERIES_LEN(out, SER_LEN(out) - 1);
            TERM_SEQUENCE(out);
        }

        Drop_Pointer_From_Series(TG_Mold_Stack, c);
        return;
    }

    // Otherwise we are molding

    Pre_Mold(mo, v);

    Append_Utf8_Codepoint(out, '[');

    mo->indent++;

    // !!! New experimental Ren-C code for the [[spec][body]] format of the
    // non-evaluative MAKE OBJECT!.

    // First loop: spec block.  This is difficult because unlike functions,
    // objects are dynamically modified with new members added.  If the spec
    // were captured with strings and other data in it as separate from the
    // "keylist" information, it would have to be updated to reflect newly
    // added fields in order to be able to run a corresponding MAKE OBJECT!.
    //
    // To get things started, we aren't saving the original spec that made
    // the object...but regenerate one from the keylist.  If this were done
    // with functions, they would "forget" their help strings in MOLDing.

    New_Indented_Line(mo);
    Append_Utf8_Codepoint(out, '[');

    REBVAL *keys_head = CTX_KEYS_HEAD(c);
    REBVAL *vars_head = CTX_VARS_HEAD(VAL_CONTEXT(v));

    bool first = true;
    REBVAL *key = keys_head;
    REBVAL *var = vars_head;
    for (; NOT_END(key); ++key, ++var) {
        if (Is_Param_Hidden(key))
            continue;
        if (GET_VAL_FLAG(var, ARG_MARKED_CHECKED))
            continue; // specialized out, don't show

        if (first)
            first = false;
        else
            Append_Utf8_Codepoint(out, ' ');

        // !!! Feature of "private" words in object specs not yet implemented,
        // but if it paralleled how <local> works for functions then it would
        // be shown as SET-WORD!
        //
        DECLARE_LOCAL (any_word);
        Init_Any_Word(any_word, REB_WORD, VAL_KEY_SPELLING(key));
        Mold_Value(mo, any_word);
    }

    Append_Utf8_Codepoint(out, ']');
    New_Indented_Line(mo);
    Append_Utf8_Codepoint(out, '[');

    mo->indent++;

    key = keys_head;
    var = vars_head;

    for (; NOT_END(key); var ? (++key, ++var) : ++key) {
        if (Is_Param_Hidden(key))
            continue;
        if (GET_VAL_FLAG(var, ARG_MARKED_CHECKED))
            continue; // specialized out, don't show

        // Having the key mentioned in the spec and then not being assigned
        // a value in the body is how voids are denoted.
        //
        if (var && IS_NULLED(var))
            continue;

        New_Indented_Line(mo);

        REBSTR *spelling = VAL_KEY_SPELLING(key);
        Append_Utf8_Utf8(out, STR_HEAD(spelling), STR_SIZE(spelling));

        Append_Unencoded(out, ": ");

        if (var)
            Mold_Value(mo, var);
        else
            Append_Unencoded(out, ": --optimized out--");
    }

    mo->indent--;
    New_Indented_Line(mo);
    Append_Utf8_Codepoint(out, ']');
    mo->indent--;
    New_Indented_Line(mo);
    Append_Utf8_Codepoint(out, ']');

    End_Mold(mo);

    Drop_Pointer_From_Series(TG_Mold_Stack, c);
}


//
//  Context_Common_Action_Maybe_Unhandled: C
//
// Similar to Series_Common_Action_Maybe_Unhandled().  Introduced because
// PORT! wants to act like a context for some things, but if you ask an
// ordinary object if it's OPEN? it doesn't know how to do that.
//
const REBVAL *Context_Common_Action_Maybe_Unhandled(
    REBFRM *frame_,
    REBVAL *verb
){
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    REBCTX *c = VAL_CONTEXT(value);

    switch (VAL_WORD_SYM(verb)) {

    case SYM_REFLECT: {
        REBSYM property = VAL_WORD_SYM(arg);
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH: // !!! Should this be legal?
            return Init_Integer(D_OUT, CTX_LEN(c));

        case SYM_TAIL_Q: // !!! Should this be legal?
            return Init_Logic(D_OUT, CTX_LEN(c) == 0);

        case SYM_WORDS:
            return Init_Block(D_OUT, Context_To_Array(c, 1));

        case SYM_VALUES:
            return Init_Block(D_OUT, Context_To_Array(c, 2));

        case SYM_BODY:
            return Init_Block(D_OUT, Context_To_Array(c, 3));

        // Noticeably not handled by average objects: SYM_OPEN_Q (`open?`)

        default:
            break;
        }

        break; }

    default:
        break;
    }

    return R_UNHANDLED;
}


//
//  REBTYPE: C
//
// Handles object!, module!, and error! datatypes.
//
REBTYPE(Context)
{
    const REBVAL *r = Context_Common_Action_Maybe_Unhandled(frame_, verb);
    if (r != R_UNHANDLED)
        return r;

    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    REBCTX *c = VAL_CONTEXT(value);

    switch (VAL_WORD_SYM(verb)) {

    case SYM_REFLECT: {
        REBSYM sym = VAL_WORD_SYM(arg);
        if (VAL_TYPE(value) != REB_FRAME)
            break;

        REBFRM *f = CTX_FRAME_MAY_FAIL(c);

        switch (sym) {
          case SYM_FILE: {
            REBSTR *file = FRM_FILE(f);
            if (not file)
                return nullptr;
            return Init_Word(D_OUT, file); }

          case SYM_LINE: {
            REBLIN line = FRM_LINE(f);
            if (line == 0)
                return nullptr;
            return Init_Integer(D_OUT, line); }

          case SYM_LABEL: {
            if (not f->opt_label)
                return nullptr;
            return Init_Word(D_OUT, f->opt_label); }

          case SYM_NEAR:
            return Init_Near_For_Frame(D_OUT, f);

          case SYM_ACTION: {
            return Init_Action_Maybe_Bound(
                D_OUT,
                value->payload.any_context.phase, // archetypal, so no binding
                value->extra.binding // e.g. where to return for a RETURN
            ); }

          case SYM_PARENT: {
            //
            // Only want action frames (though `pending? = true` ones count).
            //
            REBFRM *parent = f;
            while ((parent = parent->prior) != FS_BOTTOM) {
                if (Is_Action_Frame(parent)) {
                    REBCTX* ctx_parent = Context_For_Frame_May_Manage(parent);
                    RETURN (CTX_ARCHETYPE(ctx_parent));
                }
            }
            return nullptr; }

          default:
            break;
        }
        fail (Error_Cannot_Reflect(VAL_TYPE(value), arg)); }


    case SYM_APPEND:
        FAIL_IF_READ_ONLY_CONTEXT(c);
        if (not IS_OBJECT(value) and not IS_MODULE(value))
            fail (Error_Illegal_Action(VAL_TYPE(value), verb));
        Append_To_Context(c, arg);
        Move_Value(D_OUT, D_ARG(1));
        return D_OUT;

    case SYM_COPY: { // Note: words are not copied and bindings not changed!
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }

        REBU64 types;
        if (REF(types)) {
            if (IS_DATATYPE(ARG(kinds)))
                types = FLAGIT_KIND(VAL_TYPE_KIND(ARG(kinds)));
            else
                types = VAL_TYPESET_BITS(ARG(kinds));
        }
        else if (REF(deep))
            types = TS_STD_SERIES;
        else
            types = 0;

        Init_Any_Context(
            D_OUT,
            VAL_TYPE(value),
            Copy_Context_Core_Managed(c, types)
        );
        return D_OUT; }

    case SYM_SELECT:
    case SYM_FIND: {
        if (not IS_WORD(arg))
            return nullptr;

        REBCNT n = Find_Canon_In_Context(c, VAL_WORD_CANON(arg), false);
        if (n == 0)
            return nullptr;

        if (VAL_WORD_SYM(verb) == SYM_FIND)
            return Init_Bar(D_OUT); // TRUE would obscure non-LOGIC! result

        RETURN (CTX_VAR(c, n));
    }

    default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(value), verb));
}


//
//  construct: native [
//
//  "Creates an ANY-CONTEXT! instance"
//
//      spec [datatype! block! any-context!]
//          "Datatype to create, specification, or parent/prototype context"
//      body [block! any-context! blank!]
//          "keys and values defining instance contents (bindings modified)"
//      /only
//          "Values are kept as-is"
//  ]
//
REBNATIVE(construct)
//
// CONSTRUCT in Ren-C is an effective replacement for what MAKE ANY-OBJECT!
// was able to do in Rebol2 and R3-Alpha.  It takes a spec that can be an
// ANY-CONTEXT! datatype, or it can be a parent ANY-CONTEXT!, or a block that
// represents a "spec".
//
// !!! This assumes you want a SELF defined.  The entire concept of SELF
// needs heavy review, but at minimum this needs an override to match the
// `<with> return` or `<with> local` for functions.
//
// !!! This mutates the bindings of the body block passed in, should it
// be making a copy instead (at least by default, perhaps with performance
// junkies saying `construct/rebind` or something like that?
{
    INCLUDE_PARAMS_OF_CONSTRUCT;

    REBVAL *spec = ARG(spec);
    REBVAL *body = ARG(body);
    REBCTX *parent = NULL;

    enum Reb_Kind target;
    REBCTX *context;

    if (IS_GOB(spec)) {
        //
        // !!! Compatibility for `MAKE gob [...]` or `MAKE gob NxN` from
        // R3-Alpha GUI.  Start by copying the gob (minus pane and parent),
        // then apply delta to its properties from arg.  Doesn't save memory,
        // or keep any parent linkage--could be done in user code as a copy
        // and then apply the difference.
        //
        REBGOB *gob = Make_Gob();
        *gob = *VAL_GOB(spec);
        gob->pane = NULL;
        gob->parent = NULL;

        if (!IS_BLOCK(body))
            fail (Error_Bad_Make(REB_GOB, body));

        Extend_Gob_Core(gob, body);
        return Init_Gob(D_OUT, gob);
    }
    else if (IS_EVENT(spec)) {
        //
        // !!! As with GOB!, the 2-argument form of MAKE-ing an event is just
        // a shorthand for copy-and-apply.  Could be user code.
        //
        if (!IS_BLOCK(body))
            fail (Error_Bad_Make(REB_EVENT, body));

        Move_Value(D_OUT, spec); // !!! very "shallow" clone of the event
        Set_Event_Vars(
            D_OUT,
            VAL_ARRAY_AT(body),
            VAL_SPECIFIER(body)
        );
        return D_OUT;
    }
    else if (ANY_CONTEXT(spec)) {
        parent = VAL_CONTEXT(spec);
        target = VAL_TYPE(spec);
    }
    else if (IS_DATATYPE(spec)) {
        //
        // Should this be supported, or just assume OBJECT! ?  There are
        // problems trying to create a FRAME! without a function (for
        // instance), and making an ERROR! from scratch is currently dangerous
        // as well though you can derive them.
        //
        fail ("DATATYPE! not supported for SPEC of CONSTRUCT");
    }
    else {
        assert(IS_BLOCK(spec));
        target = REB_OBJECT;
    }

    // This parallels the code originally in CONSTRUCT.  Run it if the /ONLY
    // refinement was passed in.
    //
    if (REF(only)) {
        Init_Object(
            D_OUT,
            Construct_Context_Managed(
                REB_OBJECT,
                VAL_ARRAY_AT(body),
                VAL_SPECIFIER(body),
                parent
            )
        );
        return D_OUT;
    }

    // This code came from REBTYPE(Context) for implementing MAKE OBJECT!.
    // Now that MAKE ANY-CONTEXT! has been pulled back, it no longer does
    // any evaluation or creates SELF fields.  It also obeys the rule that
    // the first argument is an exemplar of the type to create only, bringing
    // uniformity to MAKE.
    //
    if (
        (target == REB_OBJECT or target == REB_MODULE)
        and (IS_BLOCK(body) or IS_BLANK(body))
    ){

        // First we scan the object for top-level set words in
        // order to make an appropriately sized context.  Then
        // we put it into an object in D_OUT to GC protect it.
        //
        context = Make_Selfish_Context_Detect_Managed(
            target, // type
            // scan for toplevel set-words
            IS_BLANK(body)
                ? cast(const RELVAL*, END_NODE) // gcc/g++ 2.95 needs (bug)
                : VAL_ARRAY_AT(body),
            parent
        );
        Init_Object(D_OUT, context);

        if (!IS_BLANK(body)) {
            //
            // !!! This binds the actual body data, not a copy of it.  See
            // Virtual_Bind_Deep_To_New_Context() for future directions.
            //
            Bind_Values_Deep(VAL_ARRAY_AT(body), context);

            DECLARE_LOCAL (temp);
            if (Do_Any_Array_At_Throws(temp, body))
                RETURN (temp); // evaluation result ignored unless thrown
        }

        return D_OUT;
    }

    // "multiple inheritance" case when both spec and body are objects.
    //
    // !!! As with most R3-Alpha concepts, this needs review.
    //
    if ((target == REB_OBJECT) && parent && IS_OBJECT(body)) {
        //
        // !!! Again, the presumption that the result of a merge is to
        // be selfish should not be hardcoded in the C, but part of
        // the generator choice by the person doing the derivation.
        //
        context = Merge_Contexts_Selfish_Managed(parent, VAL_CONTEXT(body));
        return Init_Object(D_OUT, context);
    }

    fail ("Unsupported CONSTRUCT arguments");
}
