csvscannerincludedir			= ${pkgincludedir}/scanner/csv-scanner

EXTRA_DIST += lib/scanner/CMakeLists.txt \
	lib/scanner/csv-scanner/CMakeLists.txt

csvscannerinclude_HEADERS = 			\
	lib/scanner/csv-scanner/csv-scanner.h \
	lib/scanner/csv-scanner/csv-scanner-simd.h

csvscanner_sources = 				\
	lib/scanner/csv-scanner/csv-scanner.c \
	lib/scanner/csv-scanner/csv-scanner-simd.c

include lib/scanner/csv-scanner/tests/Makefile.am
