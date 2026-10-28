# BC7Enc.dll for RAVE

RAVE's texture cache compresses its pictures (DXT1, DXT5, BC7 with the whole mip chain) through one native call,
`encode(src, width, height, out, format, level, genMips, threads)`. `rave_bc7enc.cpp` provides that call on top of
this repository's encoders by Rich Geldreich: **rgbcx** (BC1 / BC3) and **bc7enc** (BC7). MIT licence or public
domain, like the encoders - see `../LICENSE`.

- `build.cmd` - builds `BC7Enc.dll` with MSYS2 mingw64 g++ (everything linked in, no runtime DLLs).
- `build.cmd test` - also builds and runs `rave_bc7enc_test.exe`: every format at the game's levels, decoded back
  (PSNR), output size = the game's mip chain, nothing written past it, and the refusals.

The encoder files themselves are unchanged from upstream.
