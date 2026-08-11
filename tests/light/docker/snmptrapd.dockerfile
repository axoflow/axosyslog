# Minimal image providing snmptrapd (NET-SNMP) for the light E2E tests.
#
# Build:
#   docker build -t axosyslog-light-snmptrapd:latest -f tests/light/docker/snmptrapd.dockerfile tests/light/docker
#
# Used by SnmpTrapdDockerExecutor when pytest is invoked with:
#   --third-party-runner docker [--snmptrapd-image axosyslog-light-snmptrapd:latest]
#
# Note: no MIBs are installed. The tests use numeric OIDs (snmptrapd runs with
# "-On"), so "-m ALL" simply finds no extra MIBs and snmptrapd still starts and
# logs traps -- exactly as a default Debian/Ubuntu host install would.
FROM debian:stable-slim

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        snmptrapd \
    && rm -rf /var/lib/apt/lists/*

ENTRYPOINT ["snmptrapd"]
