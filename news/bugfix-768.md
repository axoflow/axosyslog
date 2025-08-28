`ebpf()`: acquire CAP_BPF before loading eBPF programs

Previously, when AxoSyslog was compiled with Linux capabilities enabled,
the `ebpf()` module was unable to load programs.
