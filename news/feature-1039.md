Filterx `glob_match()` function: this function will match a filename against
a single-, or a list of glob-style patterns.

Example:

    glob_match(filename, "*.zip");
    glob_match(filename, ["*.zip", "*.7z"]);
