# Miyoo Mini SDL2/GLESv2 patch

This repository already contains the SDL2/GLESv2 driver changes for the Miyoo Mini family. If you need to apply the changes on top of a fresh checkout of the `test_sdl2` branch, you can use the `miyoomini_sdl2.patch` file in the repository root.

## Applying on a clean tree

```sh
git clone https://github.com/Rparadise-Team/RetroArch -b test_sdl2 RA_SDL2
cd RA_SDL2
git apply --check miyoomini_sdl2.patch # optional dry-run
git apply miyoomini_sdl2.patch
```

If you prefer creating a commit, use `git am` instead:

```sh
git am < miyoomini_sdl2.patch
```

The patch was generated from commit `103ba4e06228233858289942c5a2be75f46a1f4e` against its parent (`f3bcdfc11e46a5e772f2c127cb5f61a0cc9e23f0`). It will apply cleanly as long as your working tree matches `f3bcdfc11e46a5e772f2c127cb5f61a0cc9e23f0`.
