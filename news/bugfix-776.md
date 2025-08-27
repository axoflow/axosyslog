`/string/`: fix escaping

Strings between / characters are now treated literally. The only exception is `\/`, which can be used to represent a `/` character.

This syntax allows you to construct regular expression patterns without worrying about double escaping.

Note: Because of the simplified escaping rules, you cannot represent strings that end with a single `\` character,
but such strings would not be valid regular expression patterns anyway.
