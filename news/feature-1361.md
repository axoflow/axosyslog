`geoip()` FilterX function: added, performing a MaxMind GeoIP2/GeoLite2
database lookup for an IP address, mirroring the `$(geoip2)` template
function. Unlike the template function, it returns typed FilterX values
and, given a `field` pointing to a map or array entry (or `field=""` for
the whole record), returns a nested dict/list instead of a single scalar.

```
$country = geoip($HOST_FROM);
$city = geoip($HOST_FROM, kind="city", field="city.names.en");
$location = geoip($HOST_FROM, kind="city", field="location");
$asn = geoip($HOST_FROM, kind="asn", field="autonomous_system_organization");
$custom = geoip($HOST_FROM, database="/opt/geoip/custom.mmdb", field="");
```

`kind` selects the database by its type (`country`, `city` or `asn`, `country` by default) instead of its file name,
so the same configuration works wherever the databases are installed. The kinds are mapped to files by
`${pkgdatadir}/geoip-databases.tsv` (e.g. `/usr/share/syslog-ng/geoip-databases.tsv`): it lists the usual GeoIP2,
GeoLite2 and DB-IP Lite locations, the first existing file wins. Customize it to point a kind elsewhere or to add new
kinds. If none of the files of a kind exist, a warning is logged and `geoip()` returns `null` until a database is
installed and the configuration is reloaded. Alternatively, `database` takes the path of the database explicitly.

`field` is a dot-separated path into the database record: map keys, or a numeric index for arrays (e.g.
`subdivisions.0.iso_code`). Its default is `country.iso_code`. The available fields depend on the database, for
example:

  * Country databases: `country.iso_code`, `country.names.en`, `continent.code`, `registered_country.iso_code`
  * City databases additionally: `city.names.en`, `subdivisions.0.iso_code`, `postal.code`, `location.latitude`,
    `location.longitude`, `location.time_zone`
  * ASN databases: `autonomous_system_number`, `autonomous_system_organization`

Names are localized: `names.en` can also be `names.de`, `names.fr`, `names.ja`, etc. Use `mmdblookup` or
`field=""` to see the full record of an IP address.

An IP address missing from the database, or a field missing from its record, yields `null`; an invalid IP address
is an error.

The `$(geoip2)` template function also accepts `--kind`, e.g. `$(geoip2 --kind asn --field autonomous_system_number
${HOST})`, as an alternative to `--database`. Without either, it still uses its earlier default.
