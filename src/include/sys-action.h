//
//  File: %sys-action.h
//  Summary: {action! defs AFTER %tmp-internals.h (see: %sys-rebact.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
// Using a technique strongly parallel to contexts, an action is identified
// by a series which is also its paramlist, in which the 0th element is an
// archetypal value of that ACTION!.  Unlike contexts, an action does not
// have values of its own...only parameter definitions (or "params").  The
// arguments ("args") come from finding an action's instantiation on the
// stack, and can be viewed as a context using a FRAME!.
//

#define R_IMMEDIATE \
    cast(const REBVAL*, &PG_R_Immediate)

#define R_INVISIBLE \
    cast(const REBVAL*, &PG_R_Invisible)

#define R_REDO_UNCHECKED \
    cast(const REBVAL*, &PG_R_Redo_Unchecked)

#define R_REDO_CHECKED \
    cast(const REBVAL*, &PG_R_Redo_Checked)

#define R_REFERENCE \
    cast(const REBVAL*, &PG_R_Reference)

#define R_UNHANDLED \
    END_NODE

inline static REBARR *ACT_PARAMLIST(REBACT *a) {
    assert(GET_SER_FLAG(&a->paramlist, ARRAY_FLAG_PARAMLIST));
    return &a->paramlist;
}

#define ACT_ARCHETYPE(a) \
    cast(REBVAL*, cast(REBSER*, ACT_PARAMLIST(a))->content.dynamic.data)

// Functions hold their flags in their canon value, some of which are cached
// flags put there during Make_Action().
//
// !!! Review if (and how) a HIJACK might affect these flags (?)
//
#define GET_ACT_FLAG(a, flag) \
    GET_VAL_FLAG(ACT_ARCHETYPE(a), (flag))

#define ACT_DISPATCHER(a) \
    (MISC(ACT_ARCHETYPE(a)->payload.action.details).dispatcher)

#define ACT_DETAILS(a) \
    ACT_ARCHETYPE(a)->payload.action.details

// These are indices into the details array agreed upon by actions which have
// the ACTION_FLAG_NATIVE set.
//
#define IDX_NATIVE_BODY 0 // text string source code of native (for SOURCE)
#define IDX_NATIVE_CONTEXT 1 // libRebol binds strings here (and lib)
#define IDX_NATIVE_MAX (IDX_NATIVE_CONTEXT + 1)

inline static REBVAL *ACT_PARAM(REBACT *a, REBCNT n) {
    assert(n != 0 and n < ARR_LEN(ACT_PARAMLIST(a)));
    return SER_AT(REBVAL, SER(ACT_PARAMLIST(a)), n);
}

#define ACT_NUM_PARAMS(a) \
    (cast(REBSER*, ACT_PARAMLIST(a))->content.dynamic.len - 1)

#define ACT_META(a) \
    MISC(a).meta


// *** These ACT_FACADE fetchers are called VERY frequently, so it is best
// to keep them light (as the debug build does not inline).  Integrity checks
// of the facades are deferred to the GC, see the REB_ACTION case in the
// switch(), and don't turn these into inline functions without a really good
// reason...and seeing the impact on the debug build!!! ***

#define ACT_FACADE(a) \
    LINK(a).facade

#define ACT_FACADE_NUM_PARAMS(a) \
    (cast(REBSER*, ACT_FACADE(a))->content.dynamic.len - 1)

#define ACT_FACADE_HEAD(a) \
    (cast(REBVAL*, cast(REBSER*, ACT_FACADE(a))->content.dynamic.data) + 1)

// The concept of the "underlying" function is that which has the right
// number of arguments for the frame to be built--and which has the actual
// correct paramlist identity to use for binding in adaptations.
//
// So if you specialize a plain function with 2 arguments so it has just 1,
// and then specialize the specialization so that it has 0, your call still
// needs to be building a frame with 2 arguments.  Because that's what the
// code that ultimately executes--after the specializations are peeled away--
// will expect.
//
// And if you adapt an adaptation of a function, the keylist referred to in
// the frame has to be the one for the inner function.  Using the adaptation's
// parameter list would write variables the adapted code wouldn't read.
//
// For efficiency, the underlying pointer can be derived from the "facade".
// Though the facade may not be the underlying paramlist (it could have its
// parameter types tweaked for the purposes of that composition), it will
// always have an ACTION! value in its 0 slot as the underlying function.
//
#define ACT_UNDERLYING(a) \
    ACT(ARR_HEAD(ACT_FACADE(a))->payload.action.paramlist)


// An efficiency trick makes functions that do not have exemplars NOT store
// nullptr in the LINK(info).specialty node in that case--instead the facade.
// This makes Push_Action() slightly faster in assigning f->special.
//
inline static REBCTX *ACT_EXEMPLAR(REBACT *a) {
    REBARR *details = ACT_ARCHETYPE(a)->payload.action.details;
    REBARR *specialty = LINK(details).specialty;
    if (GET_SER_FLAG(specialty, ARRAY_FLAG_VARLIST))
        return CTX(specialty);

    return nullptr;
}

inline static REBVAL *ACT_SPECIALTY_HEAD(REBACT *a) {
    REBARR *details = ACT_ARCHETYPE(a)->payload.action.details;
    REBSER *s = SER(LINK(details).specialty);
    return cast(REBVAL*, s->content.dynamic.data) + 1; // skip archetype/root
}


// There is no binding information in a function parameter (typeset) so a
// REBVAL should be okay.
//
#define ACT_PARAMS_HEAD(a) \
    (cast(REBVAL*, SER(ACT_PARAMLIST(a))->content.dynamic.data) + 1)



//=////////////////////////////////////////////////////////////////////////=//
//
//  ACTION!
//
//=////////////////////////////////////////////////////////////////////////=//

#ifdef NDEBUG
    #define ACTION_FLAG(n) \
        FLAG_LEFT_BIT(TYPE_SPECIFIC_BIT + (n))
#else
    #define ACTION_FLAG(n) \
        (FLAG_LEFT_BIT(TYPE_SPECIFIC_BIT + (n)) | FLAG_KIND_BYTE(REB_ACTION))
#endif

// RETURN in the last paramlist slot
//
#define ACTION_FLAG_RETURN ACTION_FLAG(0)

// Uses the Voider_Dispatcher() (implies ACTION_FLAG_RETURN + arity-0 RETURN)
//
#define ACTION_FLAG_VOIDER ACTION_FLAG(1)

// DEFERS_LOOKBACK_ARG flag is a cached property, which tells you whether a
// function defers its first real argument when used as a lookback.  Because
// lookback dispatches cannot use refinements at this time, the answer is
// static for invocation via a plain word.  This property is calculated at
// the time of Make_Action().
//
#define ACTION_FLAG_DEFERS_LOOKBACK ACTION_FLAG(2)

// This is another cached property, needed because lookahead/lookback is done
// so frequently, and it's quicker to check a bit on the function than to
// walk the parameter list every time that function is called.
//
#define ACTION_FLAG_QUOTES_FIRST_ARG ACTION_FLAG(3)

// Native functions are flagged that their dispatcher represents a native in
// order to say that their ACT_DETAILS() follow the protocol that the [0]
// slot is "equivalent source" (may be a TEXT!, as in user natives, or a
// BLOCK!).  The [1] slot is a module or other context into which APIs like
// rebRun() etc. should consider for binding, in addition to lib.  A BLANK!
// in the 1 slot means no additional consideration...bind to lib only.
//
#define ACTION_FLAG_NATIVE ACTION_FLAG(4)

#define ACTION_FLAG_UNUSED_5 ACTION_FLAG(5)

// This flag is set when the native (e.g. extensions) can be unloaded
//
#define ACTION_FLAG_UNLOADABLE_NATIVE ACTION_FLAG(6)

// An "invisible" function is one that does not touch its frame output cell,
// leaving it completely alone.  This is how `10 comment ["hi"] + 20` can
// work...if COMMENT destroyed the 10 in the output cell it would be lost and
// the addition could no longer work.
//
// !!! One property considered for invisible items was if they might not be
// quoted in soft-quoted positions.  This would require fetching something
// that might not otherwise need to be fetched, to test the flag.  Review.
//
#define ACTION_FLAG_INVISIBLE ACTION_FLAG(7)

// ^--- !!! STOP AT ACTION_FLAG(7) !!! ---^

// These are the flags which are scanned for and set during Make_Action
//
#define ACTION_FLAG_CACHED_MASK \
    (ACTION_FLAG_DEFERS_LOOKBACK | ACTION_FLAG_QUOTES_FIRST_ARG \
        | ACTION_FLAG_INVISIBLE)


inline static REBACT *VAL_ACTION(const RELVAL *v) {
    assert(IS_ACTION(v));
    REBSER *s = SER(v->payload.action.paramlist);
    if (GET_SER_INFO(s, SERIES_INFO_INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return ACT(s);
}

#define VAL_ACT_PARAMLIST(v) \
    ACT_PARAMLIST(VAL_ACTION(v))

#define VAL_ACT_NUM_PARAMS(v) \
    ACT_NUM_PARAMS(VAL_ACTION(v))

#define VAL_ACT_PARAMS_HEAD(v) \
    ACT_PARAMS_HEAD(VAL_ACTION(v))

#define VAL_ACT_PARAM(v,n) \
    ACT_PARAM(VAL_ACTION(v), n)

inline static REBARR *VAL_ACT_DETAILS(const RELVAL *v) {
    assert(IS_ACTION(v));
    return v->payload.action.details;
}

inline static REBNAT VAL_ACT_DISPATCHER(const RELVAL *v) {
    assert(IS_ACTION(v));
    return MISC(v->payload.action.details).dispatcher;
}

inline static REBCTX *VAL_ACT_META(const RELVAL *v) {
    assert(IS_ACTION(v));
    return MISC(v->payload.action.paramlist).meta;
}


// Native values are stored in an array at boot time.  These are convenience
// routines for accessing them, which should compile to be as efficient as
// fetching any global pointer.

#define NAT_VALUE(name) \
    (&Natives[N_##name##_ID])

#define NAT_ACTION(name) \
    VAL_ACTION(NAT_VALUE(name))


// A fully constructed action can reconstitute the ACTION! REBVAL
// that is its canon form from a single pointer...the REBVAL sitting in
// the 0 slot of the action's paramlist.
//
static inline REBVAL *Init_Action_Unbound(
    RELVAL *out,
    REBACT *a
){
  #if !defined(NDEBUG)
    Extra_Init_Action_Checks_Debug(a);
  #endif
    ENSURE_ARRAY_MANAGED(ACT_PARAMLIST(a));
    Move_Value(out, ACT_ARCHETYPE(a));
    assert(VAL_BINDING(out) == UNBOUND);
    return KNOWN(out);
}

static inline REBVAL *Init_Action_Maybe_Bound(
    RELVAL *out,
    REBACT *a,
    REBNOD *binding // allowed to be UNBOUND
){
  #if !defined(NDEBUG)
    Extra_Init_Action_Checks_Debug(a);
  #endif
    ENSURE_ARRAY_MANAGED(ACT_PARAMLIST(a));
    Move_Value(out, ACT_ARCHETYPE(a));
    assert(VAL_BINDING(out) == UNBOUND);
    INIT_BINDING(out, binding);
    return KNOWN(out);
}
