# vecfile — Makefile
# Requires: cosmocc 4.0.2 in .cosmocc/

COSMOCC   = .cosmocc
TOOLCHAIN = $(COSMOCC)/bin
CC        = $(TOOLCHAIN)/cosmocc
CXX       = $(TOOLCHAIN)/cosmoc++
AR        = $(TOOLCHAIN)/ar.ape

BUILDDIR = build

# ── Include paths ────────────────────────────────────────────
INCLUDES = -Ivendor/sqlite \
           -Ivendor/sqlite-vec \
           -Isrc \
           -Ivendor/llama.cpp/include \
           -Ivendor/llama.cpp/ggml/include \
           -Ivendor/llama.cpp/ggml/src \
           -Ivendor/llama.cpp/ggml/src/ggml-cpu \
           -Ivendor/llama.cpp/src

# ── Our code (strict warnings) ──────────────────────────────
OUR_CFLAGS   = -Os -g -Wall -Wextra -Werror $(INCLUDES)
OUR_CXXFLAGS = -Os -g -Wall -Wextra -Werror $(INCLUDES) -std=gnu++17

# ── Vendor code (relaxed) ───────────────────────────────────
VENDOR_CFLAGS   = -Os -g $(INCLUDES) -Wno-unused-parameter -Wno-unused-function
VENDOR_CXXFLAGS = -Os -g $(INCLUDES) -std=gnu++17 -frtti -fexceptions \
                  -Wno-unused-parameter -Wno-unused-function \
                  -Wno-deprecated-declarations

# ── SQLite flags ─────────────────────────────────────────────
SQLITE_FLAGS = -DSQLITE_ENABLE_FTS5 \
               -DSQLITE_CORE \
               -DSQLITE_THREADSAFE=0 \
               -DSQLITE_OMIT_LOAD_EXTENSION

# ── sqlite-vec flags ────────────────────────────────────────
VEC_FLAGS = -DSQLITE_CORE -DSQLITE_VEC_STATIC

# ── ggml/llama.cpp flags ────────────────────────────────────
GGML_FLAGS = -DGGML_USE_CPU \
             -DGGML_CPU_GENERIC \
             -DGGML_SCHED_MAX_COPIES=4 \
             -DNDEBUG \
             -DGGML_VERSION='"vendored"' \
             -DGGML_COMMIT='"vendored"' \
             -DLLAMA_VERSION='"vendored"' \
             -DLLAMA_COMMIT='"vendored"'

# Hot-path optimization (ggml core, quantization, ops)
GGML_HOT_CFLAGS   = -O3 $(INCLUDES) -Wno-unused-parameter $(GGML_FLAGS)
GGML_HOT_CXXFLAGS = -O3 $(INCLUDES) -std=gnu++17 -frtti -fexceptions \
                     -Wno-unused-parameter -Wno-unused-function \
                     -Wno-deprecated-declarations $(GGML_FLAGS)

ZIPOBJ    = $(TOOLCHAIN)/zipobj

LDFLAGS = -lm

# ── Bundled model ────────────────────────────────────────────
MODEL_GGUF = models/default.gguf

# ── Source files ─────────────────────────────────────────────

# GGML C sources
GGML_C_SRCS = vendor/llama.cpp/ggml/src/ggml.c \
              vendor/llama.cpp/ggml/src/ggml-alloc.c \
              vendor/llama.cpp/ggml/src/ggml-quants.c \
              vendor/llama.cpp/ggml/src/ggml-cpu/ggml-cpu.c \
              vendor/llama.cpp/ggml/src/ggml-cpu/quants.c

# GGML C++ sources
GGML_CXX_SRCS = vendor/llama.cpp/ggml/src/ggml.cpp \
                vendor/llama.cpp/ggml/src/ggml-backend.cpp \
                vendor/llama.cpp/ggml/src/ggml-backend-dl.cpp \
                vendor/llama.cpp/ggml/src/ggml-backend-meta.cpp \
                vendor/llama.cpp/ggml/src/ggml-backend-reg.cpp \
                vendor/llama.cpp/ggml/src/ggml-opt.cpp \
                vendor/llama.cpp/ggml/src/ggml-threading.cpp \
                vendor/llama.cpp/ggml/src/gguf.cpp \
                vendor/llama.cpp/ggml/src/ggml-cpu/ggml-cpu.cpp \
                vendor/llama.cpp/ggml/src/ggml-cpu/binary-ops.cpp \
                vendor/llama.cpp/ggml/src/ggml-cpu/hbm.cpp \
                vendor/llama.cpp/ggml/src/ggml-cpu/ops.cpp \
                vendor/llama.cpp/ggml/src/ggml-cpu/repack.cpp \
                vendor/llama.cpp/ggml/src/ggml-cpu/traits.cpp \
                vendor/llama.cpp/ggml/src/ggml-cpu/unary-ops.cpp \
                vendor/llama.cpp/ggml/src/ggml-cpu/vec.cpp \
                vendor/llama.cpp/ggml/src/ggml-cpu/amx/amx.cpp \
                vendor/llama.cpp/ggml/src/ggml-cpu/amx/mmq.cpp

# llama.cpp core sources
LLAMA_SRCS = $(wildcard vendor/llama.cpp/src/*.cpp)

# Model architecture sources
MODEL_SRCS = $(wildcard vendor/llama.cpp/src/models/*.cpp)

# ── Object files ─────────────────────────────────────────────
# Use subst to create unique obj paths preserving directory structure
GGML_C_OBJS   = $(patsubst vendor/%.c,$(BUILDDIR)/%.c.o,$(GGML_C_SRCS))
GGML_CXX_OBJS = $(patsubst vendor/%.cpp,$(BUILDDIR)/%.cpp.o,$(GGML_CXX_SRCS))
LLAMA_OBJS    = $(patsubst vendor/%.cpp,$(BUILDDIR)/%.cpp.o,$(LLAMA_SRCS))
MODEL_OBJS    = $(patsubst vendor/%.cpp,$(BUILDDIR)/%.cpp.o,$(MODEL_SRCS))

ALL_OBJS = $(BUILDDIR)/main.o \
           $(BUILDDIR)/embed.cpp.o \
           $(BUILDDIR)/schema.o \
           $(BUILDDIR)/ingest.o \
           $(BUILDDIR)/query.o \
           $(BUILDDIR)/sqlite3.o \
           $(BUILDDIR)/sqlite-vec.o \
           $(BUILDDIR)/model.zip.o \
           $(GGML_C_OBJS) $(GGML_CXX_OBJS) \
           $(LLAMA_OBJS) $(MODEL_OBJS)

# ── Targets ──────────────────────────────────────────────────

.PHONY: all clean setup

all: vecfile

# Our code
$(BUILDDIR)/main.o: src/main.c | $(BUILDDIR)
	$(CC) $(OUR_CFLAGS) $(SQLITE_FLAGS) $(VEC_FLAGS) -c -o $@ $<

$(BUILDDIR)/embed.cpp.o: src/embed.cpp | $(BUILDDIR)
	$(CXX) $(OUR_CXXFLAGS) $(GGML_FLAGS) -c -o $@ $<

$(BUILDDIR)/schema.o: src/schema.c | $(BUILDDIR)
	$(CC) $(OUR_CFLAGS) $(SQLITE_FLAGS) -c -o $@ $<

$(BUILDDIR)/ingest.o: src/ingest.c | $(BUILDDIR)
	$(CC) $(OUR_CFLAGS) $(SQLITE_FLAGS) -c -o $@ $<

$(BUILDDIR)/query.o: src/query.c | $(BUILDDIR)
	$(CC) $(OUR_CFLAGS) $(SQLITE_FLAGS) -c -o $@ $<

# SQLite
$(BUILDDIR)/sqlite3.o: vendor/sqlite/sqlite3.c | $(BUILDDIR)
	$(CC) $(VENDOR_CFLAGS) $(SQLITE_FLAGS) -c -o $@ $<

# sqlite-vec
$(BUILDDIR)/sqlite-vec.o: vendor/sqlite-vec/sqlite-vec.c | $(BUILDDIR)
	$(CC) $(VENDOR_CFLAGS) $(VEC_FLAGS) -c -o $@ $<

# Bundled model weights — need both arch objects for fat APE
$(BUILDDIR)/model.zip.o: $(MODEL_GGUF) | $(BUILDDIR)
	@mkdir -p $(BUILDDIR)/.aarch64
	$(ZIPOBJ) -0 -a x86_64 -N "models/default.gguf" -o $@ $<
	$(ZIPOBJ) -0 -a aarch64 -N "models/default.gguf" -o $(BUILDDIR)/.aarch64/model.zip.o $<

# ggml C files — hot path (O3)
$(BUILDDIR)/llama.cpp/ggml/src/ggml.c.o: vendor/llama.cpp/ggml/src/ggml.c
	@mkdir -p $(dir $@)
	$(CC) $(GGML_HOT_CFLAGS) -c -o $@ $<

$(BUILDDIR)/llama.cpp/ggml/src/ggml-quants.c.o: vendor/llama.cpp/ggml/src/ggml-quants.c
	@mkdir -p $(dir $@)
	$(CC) $(GGML_HOT_CFLAGS) -c -o $@ $<

$(BUILDDIR)/llama.cpp/ggml/src/ggml-cpu/quants.c.o: vendor/llama.cpp/ggml/src/ggml-cpu/quants.c
	@mkdir -p $(dir $@)
	$(CC) $(GGML_HOT_CFLAGS) -c -o $@ $<

# ggml C files — normal
$(BUILDDIR)/llama.cpp/ggml/src/ggml-alloc.c.o: vendor/llama.cpp/ggml/src/ggml-alloc.c
	@mkdir -p $(dir $@)
	$(CC) $(VENDOR_CFLAGS) $(GGML_FLAGS) -c -o $@ $<

$(BUILDDIR)/llama.cpp/ggml/src/ggml-cpu/ggml-cpu.c.o: vendor/llama.cpp/ggml/src/ggml-cpu/ggml-cpu.c
	@mkdir -p $(dir $@)
	$(CC) $(GGML_HOT_CFLAGS) -c -o $@ $<

# ggml C++ files — hot path
$(BUILDDIR)/llama.cpp/ggml/src/ggml-cpu/ops.cpp.o: vendor/llama.cpp/ggml/src/ggml-cpu/ops.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(GGML_HOT_CXXFLAGS) -c -o $@ $<

$(BUILDDIR)/llama.cpp/ggml/src/ggml-cpu/vec.cpp.o: vendor/llama.cpp/ggml/src/ggml-cpu/vec.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(GGML_HOT_CXXFLAGS) -c -o $@ $<

$(BUILDDIR)/llama.cpp/ggml/src/ggml-cpu/binary-ops.cpp.o: vendor/llama.cpp/ggml/src/ggml-cpu/binary-ops.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(GGML_HOT_CXXFLAGS) -c -o $@ $<

$(BUILDDIR)/llama.cpp/ggml/src/ggml-cpu/unary-ops.cpp.o: vendor/llama.cpp/ggml/src/ggml-cpu/unary-ops.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(GGML_HOT_CXXFLAGS) -c -o $@ $<

# ggml C++ files — pattern rule for the rest
$(BUILDDIR)/llama.cpp/ggml/src/%.cpp.o: vendor/llama.cpp/ggml/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(VENDOR_CXXFLAGS) $(GGML_FLAGS) -c -o $@ $<

# llama.cpp src/ — pattern rule
$(BUILDDIR)/llama.cpp/src/%.cpp.o: vendor/llama.cpp/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(VENDOR_CXXFLAGS) $(GGML_FLAGS) -c -o $@ $<

# Link
vecfile: $(ALL_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -f vecfile vecfile.dbg vecfile.aarch64.elf vecfile.com.dbg
	rm -rf $(BUILDDIR)

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
