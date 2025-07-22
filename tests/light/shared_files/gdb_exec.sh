#!/bin/bash -x
syslog_ng_binary_path="$1"
start_params="$2"

exec gdb <<EOF
file ${syslog_ng_binary_path}
run ${start_params}
thread apply all bt full
quit
EOF
