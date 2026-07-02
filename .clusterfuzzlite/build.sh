#!/bin/bash -eu
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
# ClusterFuzzLite / OSS-Fuzz build script for the AxoSyslog filterx fuzz harnesses.
#
# It runs inside the OSS-Fuzz base-builder image, which exports $CC, $CXX, $CFLAGS,
# $CXXFLAGS, $LIB_FUZZING_ENGINE, $SANITIZER, $OUT and $SRC. We rely on those for the
# compiler and sanitizer; the tree only adds libFuzzer coverage instrumentation and
# picks up the engine via $LIB_FUZZING_ENGINE (see configure.ac / --enable-fuzzing).

cd "$SRC/axosyslog"

# The core library whole-archives the bundled ivykis.
git submodule update --init lib/ivykis

NOCONFIGURE=1 ./autogen.sh

# ivykis' iv_container_of() takes member offsets through a null pointer, which the
# null and object-size checks report on every list walk. A real null dereference
# still crashes, and the address build covers out-of-bounds accesses.
if [ "$SANITIZER" = undefined ]; then
  export CFLAGS="$CFLAGS -fno-sanitize=null,object-size"
  export CXXFLAGS="$CXXFLAGS -fno-sanitize=null,object-size"
fi

# --disable-shared links libsyslog-ng and the -dlpreopen'd parser modules statically into
# each harness.
#
# --disable-jit: the harnesses drive the filterx interpreter, not the JIT, and the
# base-builder ships llvm-config so JIT would auto-enable and drag LLVM's C++ runtime into
# the C-linked pdbtool, slogkey and slogverify, breaking their link.
./configure \
  --enable-fuzzing \
  --enable-debug \
  --disable-shared \
  --disable-jit \
  --disable-manpages-install

# Build the default target, not just the harnesses. Only the default target runs
# BUILT_SOURCES first, which generates the bison grammar headers the core sources
# include. The harnesses are noinst_PROGRAMS under ENABLE_FUZZING, so they come
# along with it.
make -j"$(nproc)"

FUZZ_SRCDIR=lib/filterx/tests/fuzz
FUZZERS=$(cd "$FUZZ_SRCDIR" && for f in fuzz_*.c; do echo "${f%.c}"; done)

for f in $FUZZERS; do
  if [ -f "$FUZZ_SRCDIR/.libs/$f" ]; then
    cp "$FUZZ_SRCDIR/.libs/$f" "$OUT/$f"
  else
    cp "$FUZZ_SRCDIR/$f" "$OUT/$f"
  fi

  if [ -d "$FUZZ_SRCDIR/corpus/$f" ]; then
    zip -j "$OUT/${f}_seed_corpus.zip" "$FUZZ_SRCDIR/corpus/$f"/* >/dev/null
  fi
done

# --disable-shared statically links libsyslog-ng and the parser modules, but the system
# libraries (glib, pcre2, openssl, json-c, ...) stay dynamic and are absent from the
# minimal base-runner image. Bundle them next to the binaries in $OUT/lib and point the
# harnesses (and the bundled libraries themselves) there via an $ORIGIN rpath, excluding
# the C/C++ runtime that base-runner already provides.
mkdir -p "$OUT/lib"
for f in $FUZZERS; do
  ldd "$OUT/$f" 2>/dev/null | awk '/=> \//{print $3}'
done | sort -u | while read -r so; do
  case "$so" in
    */libc.so*|*/libm.so*|*/libpthread.so*|*/libdl.so*|*/librt.so*|*/ld-linux*|\
    */libresolv.so*|*/libgcc_s.so*|*/libstdc++.so*) ;;
    *) cp -u "$so" "$OUT/lib/" ;;
  esac
done
for f in $FUZZERS; do
  patchelf --set-rpath '$ORIGIN/lib' "$OUT/$f"
done
for so in "$OUT"/lib/*.so*; do
  patchelf --set-rpath '$ORIGIN' "$so" 2>/dev/null || true
done
