REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: Other Definitions"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        This code is evaluated just after actions, natives, sysobj, and
        other lower level definitions. This file intializes a minimal working
        environment that is used for the rest of the boot.
    }
    Note: {
        Any exported SET-WORD!s must be themselves "top level". This hampers
        procedural code here that would like to use tables to avoid repeating
        itself.  This means variadic approaches have to be used that quote
        SET-WORD!s living at the top level, inline after the function call.
    }
]

; Start with basic debugging

c-break-debug: :c-debug-break ;-- easy to mix up

??: ;; shorthand form to use in debug sessions, not intended to be committed
probe: func [
    {Debug print a molded value and returns that same value.}

    return: "Same as the input value"
        [<opt> any-value!]
    value [<opt> any-value!]
][
    either set? 'value [
        write-stdout mold :value
    ][
        write-stdout "// null" ;; MOLD won't take voids
    ]
    write-stdout newline
    :value
]


; COMPOSE specializes CONCOCT; add help via REDESCRIBE later
;
compose: specialize 'concoct [pattern: quote ()]
composeII: specialize 'concoct [pattern: quote (())]

; Convenience helper for making enfixed functions

set/enfix quote enfix: func [
    "Convenience version of SET/ENFIX, e.g `+: enfix :add`"
    return: <void> "`x: y: enfix :z` wouldn't enfix x, so returns void"
    :target [set-word! set-path!]
    action [action!]
][
    set/enfix target :action
]


; Common "Invisibles"

comment: enfix func [
    {Ignores the argument value, but does no evaluation (see also ELIDE).}

    return: []
        {The evaluator will skip over the result (not seen, not even void)}
    #returned [<opt> <end> any-value!]
        {The returned value.} ;-- by protocol of enfixed `return: []`
    :discarded [block! any-string! binary! any-scalar!]
        "Literal value to be ignored." ;-- `comment print "hi"` disallowed
][
]

elide: func [
    {Argument is evaluative, but discarded (see also COMMENT).}

    return: []
        {The evaluator will skip over the result (not seen, not even void)}
    discarded [<opt> any-value!]
        {Evaluated value to be ignored.}
][
]

nihil: enfix func [
    {Arity-0 form of COMMENT}
    return: [] {Evaluator will skip result}
][
]

end: func [
    {Inertly consumes all subsequent data, evaluating to previous result.}

    return: []
    :omit [any-value! <...>]
][
    while-not [tail? omit] [take omit]
]

uneval: func [
    {Make expression that when evaluated, will produce the input}

    return: {`(null)` if null, or `(quote ...)` where ... is passed-in cell}
        [group!]
    optional [<opt> any-value!]
][
    either null? :optional [quote (null)] [reduce quote ('quote :optional)]
]


; !!! NEXT and BACK seem somewhat "noun-like" and desirable to use as variable
; names, but are very entrenched in Rebol history.  Also, since they are
; specializations they don't fit easily into the NEXT OF SERIES model--this
; is a problem which hasn't been addressed.
;
next: specialize 'skip [
    offset: 1
    only: true ;-- don't clip (return null if already at head of series)
]
back: specialize 'skip [
    offset: -1
    only: true ;-- don't clip (return null if already at tail of series)
]

bound?: chain [specialize 'reflect [property: 'binding] | :value?]

unspaced: specialize 'delimit [delimiter: ""]
spaced: specialize 'delimit [delimiter: space]

an: func [
    {Prepends the correct "a" or "an" to a string, based on leading character}
    value <local> s
][
    head of insert (s: form value) either (find "aeiou" s/1) ["an "] ["a "]
]


; !!! REDESCRIBE not defined yet
;
; head?
; {Returns TRUE if a series is at its beginning.}
; series [any-series! gob! port!]
;
; tail?
; {Returns TRUE if series is at or past its end; or empty for other types.}
; series [any-series! object! gob! port! bitset! map! blank! varargs!]
;
; past?
; {Returns TRUE if series is past its end.}
; series [any-series! gob! port!]
;
; open?
; {Returns TRUE if port is open.}
; port [port!]

head?: specialize 'reflect [property: 'head?]
tail?: specialize 'reflect [property: 'tail?]
past?: specialize 'reflect [property: 'past?]
open?: specialize 'reflect [property: 'open?]


empty?: func [
    {TRUE if empty or BLANK!, or if series is at or beyond its tail.}
    return: [logic!]
    series [any-series! object! gob! port! bitset! map! blank!]
][
    did any [blank? series | tail? series]
]


eval func [
    {Make fast type testing functions (variadic to quote "top-level" words)}
    return: <void>
    'set-word... [set-word! <...>]
    <local>
        set-word type-name tester meta
][
    while [set-word: try take* set-word...] [
        type-name: copy as text! set-word
        change back tail of type-name "!" ;-- change ? at tail to !
        tester: typechecker (get bind (as word! type-name) set-word)
        set set-word :tester

        set-meta :tester construct system/standard/action-meta [
            description: spaced [{Returns TRUE if the value is} an type-name]
            return-type: [logic!]
        ]
    ]
]
    void?:
    blank?:
    bar?:
    lit-bar?:
    logic?:
    integer?:
    decimal?:
    percent?:
    money?:
    char?:
    pair?:
    tuple?:
    time?:
    date?:
    word?:
    set-word?:
    get-word?:
    lit-word?:
    refinement?:
    issue?:
    binary?:
    text?:
    file?:
    email?:
    url?:
    tag?:
    bitset?:
    image?:
    vector?:
    block?:
    group?:
    path?:
    set-path?:
    get-path?:
    lit-path?:
    map?:
    datatype?:
    typeset?:
    action?:
    varargs?:
    object?:
    frame?:
    module?:
    error?:
    port?:
    gob?:
    event?:
    handle?:
    struct?:
    library?:

    ; Typesets predefined during bootstrap.

    any-string?:
    any-word?:
    any-path?:
    any-context?:
    any-number?:
    any-series?:
    any-scalar?:
    any-array?:
|


print: func [
    {Textually output value (evaluating elements if a block), adds newline}

    return: "NULL if blank input, otherwise VOID!"
        [<opt> void!]
    line "Line of text or block to run SPACED on, blank prints nothing"
        [blank! text! block!]
][
    write-stdout switch type of line [
        blank! [return null] ;-- don't print the newline
        text! [line]
        block! [try spaced line]
    ]
    write-stdout newline
    return
]

print-newline: specialize 'write-stdout [value: newline]


decode-url: _ ; set in sys init

; used only by Ren-C++ as a test of how to patch the lib context prior to
; boot at the higher levels.
test-rencpp-low-level-hook: _

internal!: make typeset! [
    handle!
]

immediate!: make typeset! [
    ; Does not include internal datatypes
    blank! logic! any-scalar! date! any-word! datatype! typeset! event!
]

ok?: func [
    "Returns TRUE on all values that are not ERROR!"
    value [<opt> any-value!]
][
    not error? :value
]

; Convenient alternatives for readability
;
neither?: :nand?
both?: :and?
