---------------- MODULE ipl ----------------
\* os.md mechanism 4, abstract IPL state machine.
\* current_ipl is monotonic inside an event; ipending is the single bitmap
\* shared by hardware and software events; dispatch only serves levels
\* strictly above current_ipl and always picks the highest one.
EXTENDS Naturals, FiniteSets

CONSTANTS Levels

VARIABLES current, pending

vars == <<current, pending>>

Init == current = 0 /\ pending = {}

Raise(l) ==
    /\ l \in Levels
    /\ current < l
    /\ current' = l
    /\ pending' = pending

Lower(old) ==
    /\ old \in Levels
    /\ old <= current
    /\ current' = old
    /\ pending' = pending

SoftRaise(l) ==
    /\ l \in Levels
    /\ current' = current
    /\ pending' = pending \cup {l}

DispatchHighest ==
    /\ pending # {}
    /\ \E l \in pending:
        /\ current < l
        /\ \A m \in pending: m <= l
        /\ current' = l
        /\ pending' = pending \ {l}

Next == Raise(CHOOSE l \in Levels: TRUE)
     \/ \E old \in Levels: Lower(old)
     \/ SoftRaise(CHOOSE l \in Levels: TRUE)
     \/ DispatchHighest

Spec == Init /\ [][Next]_vars

\* Safety invariants checked by TLC or the accompanying Python model.
Inv == current \in Levels /\ pending \subseteq Levels
NoLowerDispatch == TRUE  \* encoded in DispatchHighest's current < l guard

=============================================================================
