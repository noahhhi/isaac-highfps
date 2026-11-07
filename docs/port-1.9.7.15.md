# Repentance+ 1.9.7.15 port (in progress)

This branch is preparation only; it does not yet provide a compatible DLL.
Do not disable the original checks to run the 1.9.7.17 DLL on this version.

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

## Still to verify

- Game frame counter, entity fields, room/list/scroll offsets.
- Projection globals and pixel snap scales.
- KAGE render targets, Lua post-render callback, overlay state.
- Menu/loading behavior, 60 Hz wrapper and 30 Hz simulation cadence,
  scrolling rooms, mod HUD and Proton stability.
- Build/version fingerprint before dereferencing any version-specific global.

## WSL build handoff

Use the existing MSVC x86 build.bat through Windows interoperability when
Visual Studio Build Tools is available. The source uses SEH and naked MS
inline assembly; keep those protections. An LLVM-MinGW 20261104 Linux build
attempt failed on x86 SEH assembler labels, even with -fseh-exceptions;
do not advertise that toolchain as working or remove SEH to hide the failure.
The explicit cerrno include fixes ENOENT declaration portability.

No proprietary executable, memory dump, user saves, credentials, or generated
DLLs are included in the repository. Local analysis inputs remain on the Deck.
