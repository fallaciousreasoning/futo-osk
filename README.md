# futo-osk

A GTK 4 on-screen keyboard for phosh that types with
[FUTO Keyboard](https://github.com/futo-org/android-keyboard)'s engine:
transformer next-word prediction and autocorrect, AOSP-derived typo
correction and swipe typing, with FUTO's default look and layouts.

Built for postmarketOS on the Pixel 6/6a (nura / google-bluejay), but
nothing in it is device-specific.

## How it fits together

```
phoc ── input-method-v2 / virtual-keyboard ──┐
                                              src/osk-input-wayland.c
src/osk-keyboard.c  key area: taps, swipes,   │
        │           long-press, cursor drag   │
        ▼                                     ▼
src/osk-ime.c  composing word, autocorrect + undo, swipe, automatic
        │      spaces, double-space period, auto-caps, suggestion strip
        ▼
src/osk-predictor.c  worker thread, newest request wins
        ▼
engine/futo-engine.cpp  C API, ports the Kotlin glue (merging dictionary
        │               and transformer results, context trimming)
        ▼
engine/glue-*.cpp + fake-jni.cpp
        FUTO's unmodified JNI glue and native code (ggml/llama,
        sentencepiece, AOSP suggest), driven through a stand-in JNIEnv
```

FUTO's source is not copied here: `scripts/fetch-futo.sh` fetches a pinned
commit, and the build compiles it in place, taking the source list from
FUTO's own `NativeFileList.cmake`.

The window is a layer-shell surface in the `osk` namespace, which phoc
treats as the on-screen keyboard. The app owns `sm.puri.OSK0` on the
session bus, so phosh's keyboard button works.

## Build and try it

```sh
scripts/fetch-futo.sh          # FUTO source, English dictionary and model (~75 MB)
meson setup build -Dfuto_resources=$PWD/futo/resources
ninja -C build
meson test -C build            # scripted typing through the whole stack, headless

# Engine alone: correct a typo, predict, decode a swipe
build/engine/futo-engine-cli futo/resources/raw/main_en.dict \
    futo/resources/raw/ml4_q6_k.gguf "I would like to" hwlp
build/engine/futo-engine-cli futo/resources/raw/main_en.dict - "" keyboard swipe

# The whole keyboard in a window with its own text field
build/src/futo-osk --test --data-dir <dir containing en/main.dict, en/model.gguf>
```

`gtk4-layer-shell` is optional at build time. Without it, the keyboard runs
as a normal window.

## On the phone

```sh
scripts/stage-pmaports.sh ~/dev/pmaports      # writes temp/futo-osk/
pmbootstrap build futo-osk
pmbootstrap sideload futo-osk
```

The package provides `phosh-osk-stub` and replaces stevia's
`sm.puri.OSK0.desktop`, so after `apk del stevia` and a re-login, phosh
starts futo-osk. `--row-height` overrides the automatic key height.

## Licence

The keyboard links FUTO's native code and ships its dictionary and model,
which are under the
[FUTO Source First License 1.1](https://github.com/futo-org/android-keyboard/blob/master/LICENSE.md).
In short: use and modification are for non-commercial purposes only, any
distribution must be free of charge and include that licence, and
modified copies must say they are modified. This is a modified derivative
of FUTO Keyboard, and is not made or endorsed by FUTO.

`engine/jni.h` is from the Android NDK (Apache-2.0). The protocol XML
files are from wlroots (MIT).

## Not done yet

- Not yet run on the phone. Everything below the Wayland layer is tested
  on desktop through `--test` and the harness.
- English only. Other FUTO dictionaries and models would need per-locale
  layouts.
- FUTO's newer neural swipe decoder (ExecuTorch `.pte` models) isn't used.
  Swipes go through the AOSP gesture decoder that FUTO also ships.
- No settings UI, emoji, clipboard, voice input or personal dictionary.
