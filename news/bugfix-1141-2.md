`filterx`: fix out-of-bounds read in `regexp_subst()` on an empty subject

A zero-length match while substituting on an empty subject advanced past the end of
the string, so the trailing copy was given a huge length. The copies are now bounded
by the subject length.
