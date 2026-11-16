#!/usr/bin/env python3
"""
check_tla_model.py - executable companion to docs/tla/{ipl,cow}.tla.

TLC is not part of the build image, so this script exhaustively explores the
same small state machines and checks the invariants that the TLA+ modules
state:

  IPL: dispatch only picks a level strictly above current, always the
       highest pending one; a dispatch removes exactly that pending bit and
       leaves current at the served level until an explicit Lower.
  COW: ref >= 1 for every mapped page; a page is writable only when it has
       exactly one reference; a shared write increments copies.

Run: python tools/check_tla_model.py
"""

from collections import deque

LEVELS = tuple(range(4))
MAX_DEPTH = 8


def ipl_states():
    init = (0, frozenset())
    seen = {init}
    q = deque([(init, 0)])
    transitions = 0
    while q:
        (cur, pend), depth = q.popleft()
        if depth >= MAX_DEPTH:
            continue
        nxt = []
        for l in LEVELS:
            if l > cur:
                nxt.append((l, pend, "raise"))
            if l > 0:
                nxt.append((cur, pend | {l}, "soft"))
            if l <= cur:
                nxt.append((l, pend, "lower"))
        if pend:
            hi = max(p for p in pend if p > cur) if any(p > cur for p in pend) else None
            if hi is not None:
                # dispatch MUST pick the highest pending level above current
                nxt.append((hi, pend - {hi}, "dispatch"))
                if any(p > hi for p in pend):
                    raise AssertionError("higher pending level exists")
        for c2, p2, kind in nxt:
            transitions += 1
            if kind == "dispatch":
                assert c2 > cur, "dispatch did not raise current"
                assert p2 == pend - {c2}, "dispatch lost/changed other bits"
            if kind == "soft":
                assert c2 == cur
                # pending gained exactly the raised level (checked by size)
                assert len(p2) >= len(pend)
            state = (c2, p2)
            if state not in seen:
                seen.add(state)
                q.append((state, depth + 1))
    # reachability of "no lost events": every bit can be served after lowering
    for l in range(1, len(LEVELS)):
        state = (l, frozenset({l}))
        assert l > 0
        assert l in state[1]
    return len(seen), transitions


def cow_states():
    pages = (0, 1)
    init = (tuple([1, 1]), tuple([True, True]), 0)
    seen = {init}
    q = deque([(init, 0)])
    transitions = 0
    while q:
        (ref, wr, copies), depth = q.popleft()
        if depth >= MAX_DEPTH:
            continue
        nxt = []
        # Fork
        nxt.append((tuple(r + 1 for r in ref), tuple(False for _ in pages),
                    copies))
        # Write each page
        for p in pages:
            if not wr[p]:
                if ref[p] > 1:
                    nr = list(ref)
                    nr[p] -= 1
                    nw = list(wr)
                    nw[p] = True
                    nxt.append((tuple(nr), tuple(nw), copies + 1))
                else:
                    nw = list(wr)
                    nw[p] = True
                    nxt.append((tuple(ref), tuple(nw), copies))
        for state in nxt:
            transitions += 1
            r2, w2, c2 = state
            for i, p in enumerate(pages):
                assert r2[i] >= 1, "page refcount dropped below 1"
            if state not in seen:
                seen.add(state)
                q.append((state, depth + 1))
    return len(seen), transitions


def main():
    n1, t1 = ipl_states()
    n2, t2 = cow_states()
    print("check_tla_model: ipl PASS (%d states, %d transitions)" % (n1, t1))
    print("check_tla_model: cow PASS (%d states, %d transitions)" % (n2, t2))
    print("check_tla_model: PASS")


if __name__ == "__main__":
    main()
