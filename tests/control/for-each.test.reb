; functions/control/for-each.r
(
    out: copy ""
    str: "abcdef"
    for-each i str [append out i]
    out = str
)
(
    blk: [1 2 3 4]
    sum: 0
    for-each i blk [sum: sum + i]
    sum = 10
)
; cycle return value
(
    blk: [1 2 3 4]
    true = for-each i blk [true]
)
(
    blk: [1 2 3 4]
    false = for-each i blk [false]
)
; break cycle
(
    str: "abcdef"
    for-each i str [
        num: i
        if i = #"c" [break]
    ]
    num = #"c"
)
; break return value
(
    blk: [1 2 3 4]
    null? for-each i blk [break]
)
; continue cycle
(
    success: true
    for-each i [1] [continue | success: false]
    success
)
; zero repetition
(
    success: true
    blk: []
    for-each i blk [success: false]
    success
)
; Test that return stops the loop
(
    blk: [1]
    f1: func [] [for-each i blk [return 1 2]]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    blk: [1 2]
    e: for-each i blk [num: i trap [1 / 0]]
    all [error? e num = 2]
)
; "recursive safety", "locality" and "body constantness" test in one
(for-each i [1] b: [not same? 'i b/3])
; recursivity
(
    num: 0
    for-each i [1 2 3 4 5] [
        for-each i [1 2] [num: num + 1]
    ]
    num = 10
)
(
    error? trap [for-each [:x] [] []]
)

; A LIT-WORD! does not create a new variable or binding, but a WORD! does
(
    x: 10
    sum: 0
    for-each x [1 2 3] [sum: sum + x]
    did all [x = 10 | sum = 6]
)
(
    x: 10
    sum: 0
    for-each 'x [1 2 3] [sum: sum + x]
    did all [x = 3 | sum = 6]
)
(
    x: 10
    y: 20
    sum: 0
    for-each ['x y] [1 2 3 4] [sum: sum + x + y]
    did all [x = 3 | y = 20 | sum = 10]
)

; Redundancy is checked for.  LIT-WORD! redundancy is legal because those
; words may have distinct bindings and the same spelling, and they're not
; being fabricated.
;
; !!! Note that because FOR-EACH soft quotes, the COMPOSE would be interpreted
; as the loop variable if you didn't put it in parentheses!
[#2273 (
    x: 10
    obj1: make object! [x: 20]
    obj2: make object! [x: 30]
    sum: 0
    did all [
        error? trap [for-each [x x] [1 2 3 4] [sum: sum + x]]
        error? trap [
            for-each (compose [ ;-- see above
                x (bind quote 'x obj1)
            ])[
                1 2 3 4
            ][
                sum: sum + x
            ]
        ]
        error? trap [
            for-each (compose [ ;-- see above
                (bind quote 'x obj2) x
            ])[
                1 2 3 4
            ][
                sum: sum + x
            ]
        ]
        not error? trap [
            for-each (compose [ ;-- see above
                (bind quote 'x obj1) (bind quote 'x obj2)
            ])[
                1 2 3 4
            ][
                sum: sum + obj1/x + obj2/x
            ]
        ]
        sum = 10
        obj1/x = 3
        obj2/x = 4
    ]
)]
