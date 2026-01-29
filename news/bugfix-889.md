Make message memory usage metrics more accurate: AxoSyslog keeps track of
memory usage by messages both globally and on a per queue basis. The
accounting behind those metrics were inaccurate, the value shown being
smaller than the actual memory use. These inaccuracies were fixed.