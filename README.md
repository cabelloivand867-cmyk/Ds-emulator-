# nds-emu

A Nintendo DS emulator in C++17. Goal: full DS hardware with sound.

## Milestone 1 (this commit): CPU core + boot

- ARM9 (ARMv5TE) and ARM7 (ARMv4T) interpreters, ARM + Thumb
- Banked registers, exceptions, IRQ entry through a tiny built-in BIOS stub
- CP15 basics (control, ITCM/DTCM, wait-for-interrupt)
- ARM9 / ARM7 memory maps, shared WRAM switching (WRAMCNT)
- `.nds` header parsing and direct boot (no BIOS/firmware files needed)
- Headless runner and CPU unit tests

## Roadmap

| # | Milestone | Notes |
|---|-----------|-------|
| 2 | Interrupts, timers, DMA, IPC (FIFO + sync), HLE BIOS SWIs | IME/IE/IF, halt, div/sqrt, CpuSet, LZ77... |
| 3 | Cartridge bus (ROMCTRL, KEY1/KEY2), SPI (firmware, touch, power) | Real save types: EEPROM/FLASH/FRAM |
| 4 | 2D engines A/B, VRAM bank mapping, display capture | Text/affine/extended BGs, sprites, windows, blending |
| 5 | SDL2 frontend: video, input, touch screen | Two screens, mouse = stylus |
| 6 | Sound: 16 SPU channels (PCM8/16, ADPCM, PSG, noise), capture, mic | Output through SDL audio |
| 7 | 3D engine (geometry + rasterizer) | Software first, then optional GPU |
| 8 | RTC, WiFi stub, GBA slot, save states, accuracy/timing passes | |

## Android app

`android/` is a Gradle project (Kotlin UI + the C++ core through JNI): ROM picker, both screens
stacked, stylus on the bottom screen, on-screen buttons with multi-touch, gamepad support, and
an AudioTrack output that also paces emulation. Graphics and sound are placeholders (dark
screens, silence) until the 2D/3D engines and SPU exist.

Build the APK:
- **Android Studio:** open `android/`, let it install the NDK 27 / CMake 3.22 it asks for, press Run.
- **Command line:** `cd android && gradle assembleDebug` (needs the Android SDK + NDK, JDK 17).
- **CI:** `.github/workflows/android.yml` builds a debug APK on every push.

## Build (desktop)

```
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
./build/nds game.nds [frames]
```

You supply your own legally obtained ROMs. No BIOS/firmware is required for direct boot.
