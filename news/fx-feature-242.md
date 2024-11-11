Added new RFC5424 SDATA related functions.

All of the functions require traditional syslog parsing beforehand.

* `has_sdata()`
  * Returns whether the current log has SDATA information.
  * Example: `sdata_avail = has_sdata(;)`
* `is_sdata_from_enterprise()`
  * Checks if there is SDATA that corresponds to the given enterprise ID.
  * Example: `sdata_from_6876 = is_sdata_from_enterprise("6876");`
* `get_sdata()`
  * Returns a 2 level dict of the available SDATAs.
  * Example: `sdata = get_sdata();`
  * Returns: `{"Originator@6876": {"sub": "Vimsvc.ha-eventmgr", "opID": "esxui-13c6-6b16"}}`

