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
