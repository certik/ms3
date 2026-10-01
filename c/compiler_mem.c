/*
 * compiler_mem.c - the memory helpers MSVC-compatible compilers emit, for
 * the Windows no-CRT native build only.
 *
 * cl and clang-cl lower aggregate initialization and copies of locals
 * (`T x = {0};`, struct assignment, array initializers) to calls to memset
 * and memcpy. Under corec's flags (/kernel, no /O, /nodefaultlib) nothing
 * defines them: corec's platform_windows.c, unlike platform_linux.c, ships no
 * fallbacks, so the link would fail. These adapters forward to corec's
 * base_memset/base_memcpy; no C library is involved.
 *
 * Scope (scripts/build.mjs, COMPILER_MEM_SOURCE): linked only into the
 * Windows native test runner. Linux uses corec's own fallbacks, and macOS and
 * WebAssembly need none, so this file is never compiled there and any memory
 * helper reference there still fails the build or its audit. Only the
 * helpers measured as emitted are defined (clang-cl /Od: memset, memcpy);
 * anything else stays an unresolved-symbol link error.
 *
 * No recursion: corec compiles base/mem.c without /O flags, so its byte
 * loops stay loops instead of becoming memset/memcpy calls. The Windows
 * build audits that neither this object nor corec's mem object references
 * any memory helper, and that the executable imports none from a DLL.
 *
 * Project code never calls these names directly (the source policy rejects
 * it); it calls base_memset/base_memcpy.
 */

#include <base/mem.h>

void *memset(void *dest, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

/* Allows defining these even where the compiler treats them as intrinsics. */
#if defined(_MSC_VER)
#pragma function(memset, memcpy)
#endif

void *memset(void *dest, int c, size_t n) {
    return base_memset(dest, c, n);
}

void *memcpy(void *dest, const void *src, size_t n) {
    return base_memcpy(dest, src, n);
}
