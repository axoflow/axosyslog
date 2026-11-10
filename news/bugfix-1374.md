`ebpf()`: Fixed a compilation failure of the eBPF program when the kernel of the build host declares
`bpf_stream_vprintk()` differently than the installed libbpf headers (for example libbpf 1.7.0 on a recent kernel).
