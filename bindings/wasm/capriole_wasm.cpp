// Entry point of the WebAssembly module. The exported surface is the C API (capriole.h), listed in
// EXPORTED_FUNCTIONS; this file only has to exist so that the module has a translation unit that
// pulls the static library in.

#include "capriole/capriole.h"

int main() { return capriole_abi_version() == CAPRIOLE_ABI_VERSION ? 0 : 1; }
