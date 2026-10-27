# BLAKE3 - BLAKE3 cryptographic hash function

This is a `build2` package repository for
[`BLAKE3`](https://github.com/BLAKE3-team/BLAKE3), a cryptographic hash
function whose single algorithm also serves as a message authentication code
and a key derivation function. The `libblake3` package builds the official C
implementation from the upstream `c/` subdirectory.

This file contains setup instructions and other details that are more
appropriate for development rather than consumption. If you want to use
`BLAKE3` in your `build2`-based project, then instead see the accompanying
[`PACKAGE-README.md`](libblake3/PACKAGE-README.md) file.

The development setup for `BLAKE3` uses the standard `bdep`-based workflow.
The package symlinks the upstream source code from the `upstream/` git
submodule, so clone the repository with its submodules. For example:

```
git clone --recurse-submodules .../BLAKE3.git
cd BLAKE3

bdep init -C @gcc cc config.c=gcc
bdep update
bdep test
```
