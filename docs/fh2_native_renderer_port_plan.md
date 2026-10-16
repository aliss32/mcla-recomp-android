# FH2-recompiled native renderer → MCLA port plan (draft, Faz 0)

Source: [Gabrick75/fh2-recompiled](https://github.com/Gabrick75/fh2-recompiled), cloned read-only
at `c:\Users\aliha\CodeGPT\mcla-android-port\_fh2_recompiled`. Target: `MclaAndroidPort`
(`com.mcla.recomp`, Midnight Club: LA Recompiled). Branch: `feat/fh2-native-renderer`.

## Goal

Adapt the FH2-recompiled graphics stack to MCLA and ship it as an APK on a new branch:

1. **Renderer selection in the launcher**: "Xenos (GPU emulation, current default)" and
   "MCLA Native (FH2-derived Vulkan renderer, adapted)".
2. **No FH2 residue**: game-specific addresses, hooks, constant layouts and shader sources
   must come from MCLA's own files (`default.xex`, `media/**`), never from FH2 data.
3. **Shader compilation pipeline**: port the `tools/xenos` pipeline (XenosRecomp translator +
   container extraction + `.nfsp` packer) so the shader library is built from MCLA files.

## What FH2 has (verified at clone time)

- Native renderer: `app/src/nativo/*` (~26k lines, from codepdbh/nfsmw-android ←
  StevensND/nfsmw-nx, GPL-3.0). Reads the game's PM4 command ring, draws with Vulkan,
  render targets as Vulkan images, resolves as copies, EDRAM not emulated.
- Game-specific code lives behind `FH2_RECOMP` / `NFSC_RECOMP` defines:
  CreatePixelShader `sub_82B70280`, CreateVertexShader `sub_82B70468`,
  SetVertexShader device+0x3310, SetPixelShader device+0x330C, draws `sub_82426180` etc.,
  `RunCommandBuffer` `sub_82426800`, swap `sub_82B72118`.
- Shader pipeline: `tools/xenos/recomp/XenosRecomp` (hedge-dev, MIT) translates Xenos
  microcode → HLSL; DXC compiles to SPIR-V; `tools/xenos/empaquetar.cpp` packs into
  `.nfsp`; library keyed by microcode hash. FH2 sources: 174 `.fxobj` in `media/shaders`
  + track zips + executable-carried containers.
- FH2 measured coverage: pixel 324/325 found, all compile; vertex 235/288 found, all compile.

## MCLA status today

- `mcla-native` gpu plugin exists (`mcla-recomp/src/native/*`) but is experimental/diagnostic:
  known hang `phase=resolve_drain STALLED>2s` after ~1654 draws (heartbeat evidence,
  docs/fh2_port_analysis.md). Renderer already selected via `--gpu_plugin=<id>`
  (`mcla_app.cpp:120-140`).
- FH2-based renderer will be a **separate plugin id** so it can be developed without
  destabilizing the existing one.

## Port phases

| Faz | Content | Deliverable |
|---|---|---|
| 0 | This plan (draft) | `docs/fh2_native_renderer_port_plan.md` |
| 1 | Inventory of all `FH2_RECOMP` guarded code in nativo/; map each site to the MCLA equivalent (or mark "needs RE"). | inventory doc, branch created |
| 2 | Build XenosRecomp for host/Android; fix FH2-specific translator patterns to MCLA's microcode set. | translator builds |
| 3 | Extract MCLA shader containers from our game files; build `.nfsp` shader library. | mcla_shaders.nfsp |
| 4 | Copy nativo renderer into `MclaAndroidPort` under `mcla-recomp/src/nativo/` (or a plugin dir), stripping FH2 residue: no `fh2_`/`nfsmw_` data, no FH2 addresses; license attribution kept. | source in tree |
| 5 | Implement MCLA hooks: shader create/set, draws, RunCommandBuffer equivalent, swap — from MCLA code. PM4 ring reader adaptation. | renderer draws something |
| 6 | Android build: plugin `libmcla-fhnative.so`, gradle/CMake wiring. | plugin .so builds |
| 7 | Launcher renderer selector: Xenos / MCLA Native; availability check; last_launch.txt line. | UI change |
| 8 | Verify with our game files on device; APK build. | APK |

## Licensing

- fh2-recompiled, nativo code, XenosRecomp: GPL-3.0 / MIT — compatible with our GPL-3.0
  `mcla-recomp`. Attribution required: keep "FH2-derived renderer" note in launcher +
  THIRD_PARTY_NOTICES.

## Risks / open questions

- MCLA's D3D library is the same XDK family, so the nfsmw/Carbon/FH2 hook-fingerprint method
  should transfer, but MCLA addresses must be found in our own recompiled code.
- MCLA shader container format: FH2 used the 2008 format (`10 2A 11 00/01`); MCLA's year
  (2008, same XDK generation) suggests the same format — verify in Faz 3.
- Descriptor indexing requirement (`VK_EXT_descriptor_indexing`) blocked FH2 native on some
  Adreno drivers; our Turnip proxy path may already solve this.

## Faz 1 inventory (measured, 2026-10-16)

- `app/src/nativo/`: 31 files (10 cpp, 21 h), ~1.8 MB. Game-specific code is guarded by
  `FH2_RECOMP` / `NFSC_RECOMP` / default(NFSMW); full guarded-line list exported to
  `docs/fh2_recomp_inventory.csv` (206 preprocessor branch lines). The heavy renderer files
  (`nfsmw_nativo_dibujos.cpp` 827 KB, `nfsmw_nativo_destinos.cpp` 429 KB,
  `nfsmw_nativo_sistema.cpp` 269 KB) are mostly game-agnostic PM4/Vulkan code.
- FH2-only game hooks live in `app/src/fh2_nativo_ganchos.cpp` (63 lines: hooks on
  `sub_82B70280` CreatePixelShader, `sub_82B70468` CreateVertexShader, `sub_8241ACA0` VS
  fetch patcher, `sub_8245B370` SetVertexShader) and `app/src/fh2_nativo_enlaces.cpp`
  (neutral answers to NFSMW-specific renderer questions). These map to our
  `mcla-recomp/src` side (like our `mcla-native` plugin) rather than `src/nativo`.
- Device offsets the FH2 branch fills (`nfsmw_nativo_ganchos.cpp:53-63`): device VS 0x3310,
  device PS 0x330C; NFS Carbon 0x3080/0x307C; NFSMW default 0x4FE8/0x4FE4. MCLA needs its own
  values found in our recompiled code (Faz 5).
- `REXCVAR_` cvars in nativo/: 212 across 10 files, none named `fh2_*` (the FH2-specific
  cvars live in the FH2 host code, e.g. `fh2_renderizador`, `fh2_censo`).
- FH2 builds nativo into the main executable (`app/CMakeLists.txt:27-37`) with
  `FH2_RECOMP=1` + SDK xxHash + presenter SPIR-V includes; our tree builds GPU plugins as
  separate `.so` targets (`mcla-recomp/CMakeLists.txt:73`, plugin id `mcla-native`). The
  FH2-derived renderer becomes a second plugin target `rexgpu-mcla-fhnative`
  (libmcla-fhnative.so) next to `rexgpu-mcla-native`.
- Translator: FH2 changes live under `FH2_RECOMP` in `tools/xenos/recomp/shader_recompiler.{h,cpp}`
  (36 branch sites) + `shader_common.h`; built by `tools/xenos/biblioteca.sh` with
  `-DNFSMW_RECOMP -DFH2_RECOMP`, fmt+xxHash from `sdk/thirdparty`, DXC for SPIR-V.

## Faz 2/3 status update (2026-10-16, later)

- XenosRecomp translator tree fully surveyed: `tools/xenos/recomp/XenosRecomp/shader_recompiler.cpp`
  (2441 lines; FH2_RECOMP branch sites at 342, 679, 884, 1352, 1384-1404, 1470, 1520, 1616, 1686,
  1737, 1783, 1871, 1884, 1908, 2078, 2172, 2389), `shader_common.h` (FH2_* byte-layout macros),
  `nfsmw_hlsl.cpp` (host-side main()), `empaquetar.cpp` (.nfsp packer keyed on FH2_RECOMP).
- MCLA game data reality check: the PC reference tree `C:\Users\aliha\Desktop\mcla-recomp-v1.0.1`
  ships `game/` with loose `default.xex` + `xarchive_{cache,audio,audlo,music}.rpf`. Raw byte scans
  find only ~34 scattered 0x102A container magics — the shader blocks live inside RPF entries that
  are compressed/encrypted, so a plain byte scan cannot lift them. Build the extraction on the
  emulator's own archive reader instead (MCLA xex embeds the XArchive reader; FH2's
  `extrair_contenedores.py` walks the LZX-compressed zips the same way).
- Only `xarchive_cache.rpf` matters for shaders (media/shaders-equivalent); audio archives hit the
  same magics inside compressed audio blobs (false positives).
- Deliverable for Faz 3 becomes `mcla_shaders.nfsp` + `contenedores/` directory, built by a new
  `tools/xenos_mcla/` build script mirroring `biblioteca.sh` but reading MCLA archives.

## Faz 3b status (2026-10-16, latest)

- `xarchive_*.rpf` headers parse as RPF3-like ('RPF3' magic 0x33465052, count+dirSize+0xFFFFFFFF),
  but the TOC at 0x800 onward is AES-encrypted (high-entropy bytes, no plaintext names), and the
  file payload region likewise. The decryption keys live inside `default.xex` (the game's XArchive
  reader derives them at runtime).
- Practical consequence: shader-container extraction needs either
  (a) the xex's embedded key derivation (extract from the recompiled binary we already run), or
  (b) running the MCLA PC reference build once and dumping its decrypted XArchive pages, or
  (c) the FH2 approach: use the running recompiled process itself to hook the game's own archive
    open/read calls and dump containers to disk (exactly what FH2's `app/src/fh2_*` dump hooks do).
- Chosen path: (c). A small dump plugin (built on the same plugin mechanism as `mcla-native`)
  hooks MCLA's archive-open + shader-create functions, writes every 0x102A container to
  `out/contenedores/*.bin` on first run; then the existing XenosRecomp + empaquetar chain (built
  with `MCLA_RECOMP` instead of `FH2_RECOMP`) produces `mcla_shaders.nfsp`.
- This changes Faz 3b/4/5 order: the dump plugin is Faz 4's first commit, then shader build, then
  the full native renderer.

## Faz 4a completion (2026-10-16)

Delivered in this commit:
- `mcla-recomp/src/dump/` — `rexgpu-mcla-dump` plugin skeleton (5 files): same census
  plumbing (Vulkan provider + blank present) plus a `ContainerDumper` background thread that
  walks guest physical memory every 5 s and writes every valid 0x102A shader container
  (2005/2008 formats, deduped by XXH3) to `<content>/contenedores/*.bin`.
- `mcla-recomp/CMakeLists.txt` — `rexgpu-mcla-dump` shared library target mirroring
  `rexgpu-mcla-native` (census sources + dump sources).
- `scripts/build_native.sh` — builds the new target; .so files auto-stage to jniLibs.
- `app/src/main/java/com/mcla/LauncherActivity.java` — "Renderer" button + dialog
  (Xenos emulated / MCLA native / FH2 native), persisted in PERF_PREFERENCES as
  `renderer`, summary label under the button; choice forwarded as `mcla_renderer` extra.
- `app/src/main/java/com/mcla/MclaActivity.java` — `mcla_renderer` extra replaces any
  `--gpu_plugin` line in the effective args (`--gpu_plugin=<choice>`).
- `mcla-recomp/src/mcla_app.cpp` — census-path check widened to `mcla-native || mcla-dump`.

Open items for the next phases (unchanged from the plan): nativo code carry-over with
`MCLA_RECOMP` guards (Faz 4b), MCLA address table for the four shader hooks (Faz 5),
shaders from the dumped containers via XenosRecomp+empaquetar (Faz 7), APK (Faz 8).
