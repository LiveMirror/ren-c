; better-than-nothing ADAPT tests

[
    x: 10
    foo: adapt 'any [x: 20]
    foo [1 2 3]
    x = 20
]
[
    capture: blank
    foo: adapt 'any [capture: block]
    all? [
      foo [1 2 3]
      capture = [1 2 3]
    ]
]