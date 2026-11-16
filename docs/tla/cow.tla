---------------- MODULE cow ----------------
\* os.md mechanism 2/3, abstract COW reference-count state machine.
\* A page is shared read-only after fork; a write by either side copies
\* when ref > 1, or just flips the PTE writable when ref = 1.
EXTENDS Naturals, FiniteSets

CONSTANTS Pages

VARIABLES ref, writable, copies

vars == <<ref, writable, copies>>

Init ==
    /\ ref = [p \in Pages |-> 1]
    /\ writable = [p \in Pages |-> TRUE]
    /\ copies = 0

Fork ==
    /\ ref' = [p \in Pages |-> ref[p] + 1]
    /\ writable' = [p \in Pages |-> FALSE]
    /\ copies' = copies

Write(p) ==
    /\ p \in Pages
    /\ ~writable[p]
    /\ IF ref[p] > 1
       THEN /\ ref' = [q \in Pages |-> IF q = p THEN ref[q] - 1 ELSE ref[q]]
            /\ copies' = copies + 1
            /\ writable' = [q \in Pages |-> IF q = p THEN TRUE ELSE writable[q]]
       ELSE /\ ref' = ref
            /\ copies' = copies
            /\ writable' = [q \in Pages |-> IF q = p THEN TRUE ELSE writable[q]]

Next == Fork \/ \E p \in Pages: Write(p)

Spec == Init /\ [][Next]_vars

\* No page may be released while any mapping still refers to it.
Inv == \A p \in Pages: ref[p] >= 1

=============================================================================
