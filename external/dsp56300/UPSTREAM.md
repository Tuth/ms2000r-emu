# dsp56300 (vendored, patched)

The DSP56300 emulator library by **The Usual Suspects** (https://github.com/dsp56300/dsp56300, part of the
gearmulator project), GPL-3.0 - see `LICENSE.md` in this folder.

- Upstream commit: `d6e4514727366d70c4c645cd904256783ad65c8d`
- asmjit (https://github.com/dsp56300/asmjit): `3577608cab0bc509f856ebf6e41b2f9d9f71acc4`
- Changes for the MS2000: the patch series in `../../patches/dsp56300/` (0001-0014), already applied here.
- Left out: `source/wxWidgets` (only needed for the optional debugger, `DSP56300_DEBUGGER=OFF`) and the
  1.7 MB asmjit assembler test source.
