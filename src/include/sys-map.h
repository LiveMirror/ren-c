//
//  File: %sys-map.h
//  Summary: {Definitions for REBMAP}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// Maps are implemented as a light hashing layer on top of an array.  The
// hash indices are stored in the series node's "misc", while the values are
// retained in pairs as `[key val key val key val ...]`.
//
// When there are too few values to warrant hashing, no hash indices are
// made and the array is searched linearly.  This is indicated by the hashlist
// being NULL.
//
// Though maps are not considered a series in the "ANY-SERIES!" value sense,
// they are implemented using series--and hence are in %sys-series.h, at least
// until a better location for the definition is found.
//
// !!! Should there be a MAP_LEN()?  Current implementation has NONE in
// slots that are unused, so can give a deceptive number.  But so can
// objects with hidden fields, locals in paramlists, etc.
//

struct Reb_Map {
    struct Reb_Array pairlist; // hashlist is held in ->link.hashlist
};

inline static REBARR *MAP_PAIRLIST(REBMAP *m) {
    assert(GET_SER_FLAG(&(m)->pairlist, ARRAY_FLAG_PAIRLIST));
    return (&(m)->pairlist);
}

#define MAP_HASHLIST(m) \
    (LINK(MAP_PAIRLIST(m)).hashlist)

#define MAP_HASHES(m) \
    SER_HEAD(MAP_HASHLIST(m))

inline static REBMAP *MAP(void *p) {
    REBARR *a = ARR(p);
    assert(GET_SER_FLAG(a, ARRAY_FLAG_PAIRLIST));
    return cast(REBMAP*, a);
}


inline static REBMAP *VAL_MAP(const RELVAL *v) {
    assert(IS_MAP(v));

    REBSER *s = v->payload.any_series.series;
    if (GET_SER_INFO(s, SERIES_INFO_INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());

    return MAP(s);
}

inline static REBCNT Length_Map(REBMAP *map)
{
    REBVAL *v = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));

    REBCNT count = 0;
    for (; NOT_END(v); v += 2) {
        if (not IS_NULLED(v + 1))
            ++count;
    }

    return count;
}
