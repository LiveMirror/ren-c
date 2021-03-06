REBOL []

inp: %iso3166.txt
cnt: read inp
if #{EFBBBF} = to binary! copy/part cnt 3 [ ;UTF8 BOM
    cnt: skip cnt 3
]

lower: charset [#"a" - #"z"]
letter: charset [#"a" - #"z" #"A" - #"Z"]

capitalize: func [
    n
][
    ret: copy ""
    words: split to text! n " "
    spaced [
        map-each w words [
            case [
                w = "OF" [
                    "of"
                ]
                w = "U.S." [
                    "U.S."
                ]
            ] else [
                unspaced [
                    uppercase first w
                    lowercase next w
                ]
            ]
        ]
    ]
]

iso-3166-table: make map! 512
parse cnt [
    some [
        copy name to ";"
        ";" copy code-2 to "^/" (
            append iso-3166-table pair: compose [
                (to text! code-2) (to text! capitalize name)
            ]
        )

        "^/"
    ]
]

init-code: to text! read init
space: charset " ^-^M^/"
iso-3166-table-cnt: find mold iso-3166-table #"["
parse init-code [
    thru "iso-3166-table:"
    to #"["
    change [
         #"[" thru #"]"
    ] iso-3166-table-cnt
    to end
] or [
    fail "Failed to update iso-3166-table"
]

write init init-code
