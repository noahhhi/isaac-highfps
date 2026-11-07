# Repentance+ 1.9.7.15.J374 port (beta)

This branch builds a dedicated J374 DLL, tested on Steam Deck OLED with Proton.
The J460 DLL remains incompatible: use build-wsl.sh for J374.
PE timestamp/size and all affected instruction sites are checked before patching.

## Confirmed from 1.9.7.15.J374

Image base: 0x400000. Addresses were located using REPENTOGON signatures,
then inspected in the running game's memory and disassembly.

| Item | 1.9.7.17 | 1.9.7.15 |
|---|---|---|
| g_Manager | 0xC7169C | 0xC0C09C |
| g_Game | 0xC71678 | 0xC0C074 |
| Manager frame count | +0x4ABBC | +0x4ABA8 |
| IsaacMain | 0x931050 | 0x929330 |
| Manager::Update | 0x954CD0 | 0x94CAD0 |

The manager pointer and frame counter were read live: the counter advanced
60 times per second with the native mod disabled. The old g_Manager address
falls in .reloc on 1.9.7.15, so even initialization dereferences invalid data.

The main loop in 1.9.7.15 has `push 1` at 0x9295D6 followed by
`call 0x94CAD0` at 0x9295D8, and render at 0x9295DD. It does not use
the 1.9.7.17 no-argument wrapper assumed by InstallWrapperHook.
A call-site adapter must preserve the original calling convention and stack.

Limiter: at 0x929611 `test bl,bl`, at 0x929613 `jne 0x929621`,
at 0x929615 `cmp byte ptr [0xBD5795],bl`, and at 0x92961B
`jne 0x929514`. Pacing begins at 0x929621; the loop head is 0x929514.
Do not reuse the old branch bytes/displacement. The logic gate must be
installed before uncapping, and all patch sites must be validated together.

## Build on WSL / Linux

Install clang-cl, lld-link, Python 3, and obtain an x86 Windows CRT/SDK sysroot
with xwin (`--accept-license --arch x86 splat --output /path/to/msvc`).

```sh
XWIN_ROOT=/path/to/msvc WINMM_SOURCE=/path/to/32bit/winmm.dll bash build-wsl.sh
```

The export source can be Wine's syswow64/winmm.dll. It is only parsed to generate
forwarders; do not commit it. The resulting DLL uses the static CRT and imports
only KERNEL32.dll. The tested compiler was Fedora clang-cl 22.1.8 with MSVC ABI.
GNU-ABI LLVM-MinGW failed to assemble x86 SEH for this source; use clang-cl instead.

## Install

Copy build/winmm.dll next to isaac-ng.exe. Under Proton set winmm=native,builtin
using an application DLL override or the upstream Steam launch option.
Use Enabled=1, Interpolate=1, MaxFps=90, LuaOverlay=0, Log=1 on Deck OLED.
Keep the game's VSync disabled and avoid a Steam frame cap below 90.

## Validation on Steam Deck

- Full startup, save selection, room loading, and character movement completed.
- Logs measured 90-91 render-loop iterations and 60-61 wrapper calls per
  one-second reporting interval (boundary rounding accounts for the extra one).
- Independent 3-second live-memory sampling measured 59.998 Manager ticks/s
  and 29.999 Game ticks/s, preserving normal game speed.
- During a short movement sample, 45 position changes occurred without a change
  to the Manager frame counter, confirming intermediate position previews.
- Pause automatically returned rendering to approximately 60 FPS.

## Known limitations

The J374 KAGE ABI differs from J460. This build forcibly disables LuaOverlay;
unported graphics addresses are never called. Lua presence/rate globals still
work, but render callbacks run at render rate, not the original 60 Hz. Mods that
count render callbacks may advance those counters faster. Upstream generic log
messages about the Lua gate do not imply cadence suppression with overlay off.
Large-room scrolling and lengthy combat still need broader player testing.

No proprietary executable, memory dump, user saves, credentials, or generated
DLLs are included in the repository. Analysis files remain outside the checkout.
