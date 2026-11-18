# futo-swipe-decoder

A small helper program that decodes swipes for futo-osk with
[FUTO Swipe](https://huggingface.co/futo-org/futo-swipe), FUTO's neural swipe
models. It's built on FUTO's [swipe-library](https://gitlab.futo.org/keyboard/swipe-library),
which runs the models on [ExecuTorch](https://github.com/pytorch/executorch).

futo-osk starts it when it's installed, and falls back to its dictionary
decoder when it isn't, or if it fails. On FUTO's held-out swipe data, it gets
about 92% of words right first time, against 76% for the dictionary decoder.

## Why it's a separate program

swipe-library is GPL-3.0, so this helper is too (see `LICENSE`). futo-osk
links FUTO Keyboard's code, which is under FUTO's non-commercial source-first
licence. Those terms can't be combined in one program, so they talk over a
pipe instead.

The models are under the FUTO Model Weights License. It allows any use,
including commercial, as long as users can see that the product is "powered
by FUTO Swipe". futo-osk shows this on the space bar.

## Protocol

It speaks a line protocol on stdin/stdout, described at the top of
`futo-swipe-decoder.cpp`.

## Building

The Alpine package (`packaging/alpine/futo-swipe-decoder`) builds ExecuTorch
from pinned sources with `build-executorch.sh`. It also applies
`patches/executorch-musl-assert-fail.patch`, which lets ExecuTorch build
against musl.

For development on a desktop Linux machine:

```sh
git clone --recursive https://gitlab.futo.org/keyboard/swipe-library ../swipe-library
make -C ../swipe-library host        # builds ExecuTorch too
cmake -S . -B build -DSWIPE_LIBRARY_DIR=$PWD/../swipe-library \
    -DEXECUTORCH_ROOT=$PWD/../swipe-library/third_party/executorch \
    -DET_BUILD=$PWD/../swipe-library/third_party/executorch/cmake-out-host
cmake --build build
```

Then download the `honorable_sturgeon` and `magic_macaw` folders from
[futo-org/futo-swipe](https://huggingface.co/futo-org/futo-swipe), and point
futo-osk at the program and models:

```sh
FUTO_OSK_SWIPE_DECODER=$PWD/build/futo-swipe-decoder \
FUTO_SWIPE_MODELS=/path/to/futo-swipe futo-osk
```
