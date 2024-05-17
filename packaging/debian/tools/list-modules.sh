#! /bin/sh

CORE_MODULES="axosyslog-mod-sql axosyslog-mod-mongodb"
ALL_MODULES=$(echo $(grep "^Package: axosyslog-mod-" debian/control | cut -d: -f 2))

case "$1" in
        "core")
                echo ${CORE_MODULES} | tr ' ' ','
                ;;
        "all")
                echo ${ALL_MODULES}  | tr ' ' ','
                ;;
        "optional"|"")
                OPTIONAL_MODULES=""
                for mod in ${ALL_MODULES}; do
                        if ! (echo "${CORE_MODULES}" | grep -q ${mod}); then
                                OPTIONAL_MODULES="${OPTIONAL_MODULES}${mod} "
                        fi
                done
                OPTIONAL_MODULES=$(echo ${OPTIONAL_MODULES})
                echo ${OPTIONAL_MODULES} | tr ' ' ','
                ;;
        *)
                exit 1
                ;;
esac
