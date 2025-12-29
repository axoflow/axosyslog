Fix capture group references in the replacement argument for the FilterX
`regexp_subst()` function: in the case of "global=true", the value of
capture group references were always used from that of the first match, e.g.
the 2nd and subseqent matches used an incorrect value.
