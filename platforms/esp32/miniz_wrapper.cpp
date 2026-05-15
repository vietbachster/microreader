// Compile miniz (C code) as C++ so the IDF toolchain's C++-only flags
// (e.g. -fuse-cxa-atexit) do not cause compiler errors.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
extern "C" {
#include "../../test/build2/_deps/miniz-src/miniz.c"
}
#pragma GCC diagnostic pop
