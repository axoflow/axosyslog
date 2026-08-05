set(SOCKET_HEADERS
    socket/afinet.h
    socket/afsocket-signals.h
    socket/afsocket-source.h
    socket/socket-options.h
    socket/socket-options-inet.h
    socket/transport-mapper.h
    socket/transport-mapper-inet.h
    PARENT_SCOPE)

set(SOCKET_SOURCES
    socket/afinet.c
    socket/afsocket-source.c
    socket/socket-options.c
    socket/socket-options-inet.c
    socket/transport-mapper.c
    socket/transport-mapper-inet.c
    PARENT_SCOPE)
