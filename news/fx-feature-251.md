`parse_xml()`: Added new function to parse XMLs.

Example usage:
```
my_structured_data = parse_xml(raw_xml);
```

Converting XML to a dict is not standardized.

Our intention is to create the most compact dict as possible,
which means certain nodes will have different types and
structures based on a number of different qualities of the
input XML element.

The following points will demonstrate the choices we made in our parser.
In the examples we will use the JSON dict implementation.

1. Empty XML elements become empty strings.
```
  XML:  <foo></foo>
  JSON: {"foo": ""}
```

2. Attributions are stored in `@attr` key-value pairs,
   similarly to some other converters (e.g.: python xmltodict).
```
  XML:  <foo bar="123" baz="bad"/>
  JSON: {"foo": {"@bar": "123", "@baz": "bad"}}
```

3. If an XML element has both attributes and a value, 
   we need to store them in a dict, and the value needs a key.
   We store the text value under the #text key.
```
  XML:  <foo bar="123">baz</foo>
  JSON: {"foo": {"@bar": "123", "#text": "baz"}}
```

4. An XML element can have both a value and inner elements.
   We use the `#text` key here, too.
```
  XML:  <foo>bar<baz>123</baz></foo>
  JSON: {"foo": {"#text": "bar", "baz": "123"}}
```

5. An XML element can have multiple values separated by inner elements.
   In that case we concatenate the values.
```
  XML:  <foo>bar<a></a>baz</foo>
  JSON: {"foo": {"#text": "barbaz", "a": ""}}
```

