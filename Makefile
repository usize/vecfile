# vecfile — Makefile
# Requires: cosmocc 4.0.2 in .cosmocc/

COSMOCC   = .cosmocc
TOOLCHAIN = $(COSMOCC)/bin
CC        = $(TOOLCHAIN)/cosmocc
AR        = $(TOOLCHAIN)/ar.ape

BUILDDIR = build

# Include paths: vendor/sqlite for sqlite3.h, vendor/sqlite-vec for sqlite-vec.h
INCLUDES = -Ivendor/sqlite -Ivendor/sqlite-vec

# Flags for our code (strict)
OUR_CFLAGS = -Os -g -Wall -Wextra -Werror $(INCLUDES)

# Flags for vendor code (relaxed — don't -Werror third-party sources)
VENDOR_CFLAGS = -Os -g $(INCLUDES) -Wno-unused-parameter

# SQLite compile flags
SQLITE_CFLAGS = -DSQLITE_ENABLE_FTS5 \
                -DSQLITE_CORE \
                -DSQLITE_THREADSAFE=0 \
                -DSQLITE_OMIT_LOAD_EXTENSION

# sqlite-vec compile flags
VEC_CFLAGS = -DSQLITE_CORE -DSQLITE_VEC_STATIC

LDFLAGS =

.PHONY: all clean setup

all: vecfile

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/main.o: src/main.c | $(BUILDDIR)
	$(CC) $(OUR_CFLAGS) $(SQLITE_CFLAGS) $(VEC_CFLAGS) -c -o $@ $<

$(BUILDDIR)/sqlite3.o: vendor/sqlite/sqlite3.c | $(BUILDDIR)
	$(CC) $(VENDOR_CFLAGS) $(SQLITE_CFLAGS) -c -o $@ $<

$(BUILDDIR)/sqlite-vec.o: vendor/sqlite-vec/sqlite-vec.c | $(BUILDDIR)
	$(CC) $(VENDOR_CFLAGS) $(VEC_CFLAGS) -c -o $@ $<

vecfile: $(BUILDDIR)/main.o $(BUILDDIR)/sqlite3.o $(BUILDDIR)/sqlite-vec.o
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f vecfile vecfile.dbg vecfile.aarch64.elf
	rm -rf $(BUILDDIR)

# Download and extract cosmocc if not present
setup:
	@if [ ! -d "$(COSMOCC)" ]; then \
		echo "Downloading cosmocc 4.0.2..."; \
		curl -L -o .cosmocc.zip https://cosmo.zip/pub/cosmocc/cosmocc-4.0.2.zip; \
		mkdir -p $(COSMOCC); \
		unzip -q .cosmocc.zip -d $(COSMOCC); \
		rm .cosmocc.zip; \
		echo "cosmocc ready."; \
	else \
		echo "cosmocc already present."; \
	fi
