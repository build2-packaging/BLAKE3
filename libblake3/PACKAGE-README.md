# libblake3 - BLAKE3 cryptographic hash function C library

This is a `build2` package for the
[`BLAKE3`](https://github.com/BLAKE3-team/BLAKE3) C library. It provides the
official C implementation of the BLAKE3 cryptographic hash function, whose
single algorithm also serves as a message authentication code and a key
derivation function. Its output extends to any length. BLAKE3 is a Merkle
tree on the inside, which makes it highly parallelizable across SIMD lanes.
See the upstream [`README.md`](README.md) for the API.


## Usage

To start using `libblake3` in your project, add the following `depends`
value to your `manifest`, adjusting the version constraint as appropriate:

```
depends: libblake3 ^1.8.7
```

Then import the library in your `buildfile`:

```
import libs = libblake3%lib{blake3}
```

And include its header in your source code:

```
#include <blake3.h>
```

The library detects the CPU features at runtime and uses the widest SIMD
implementation available. This package builds the following
implementations for each target:

| Target                         | SIMD implementations                     |
|--------------------------------|------------------------------------------|
| x86_64 with GCC and Clang      | SSE2, SSE4.1, AVX2, AVX-512 (assembly)   |
| x86_64 with MSVC, 32-bit x86   | SSE2, SSE4.1, AVX2, AVX-512 (intrinsics) |
| AArch64 (little-endian)        | NEON                                     |
| Other                          | Portable C only                          |

You can disable each x86 SIMD implementation with the configuration
variables below.

The package builds the library without the optional oneTBB multithreading
support, which leaves `blake3_hasher_update_tbb()` out of the header. The
only oneTBB package on cppget.org distributes prebuilt binaries.


## Configuration variables

This package provides the following configuration variables:

```
[bool] config.libblake3.sse2   ?= true
[bool] config.libblake3.sse41  ?= true
[bool] config.libblake3.avx2   ?= true
[bool] config.libblake3.avx512 ?= true
```

Each variable controls whether the library contains the corresponding x86
SIMD implementation. Disabling one removes it from the runtime dispatch, the
same as defining upstream's `BLAKE3_NO_SSE2`, `BLAKE3_NO_SSE41`,
`BLAKE3_NO_AVX2`, and `BLAKE3_NO_AVX512` macros, respectively. The variables
have no effect on other CPUs.
