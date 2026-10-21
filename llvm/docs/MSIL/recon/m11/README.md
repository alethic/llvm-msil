# M11 recon evidence

Recon for milestone M11 (ImplementationPlan.md section 6, M11; section 12 rows dated
2026-10-20). Five recon agents re-established on 2026-10-20, at `0c5044bac349`, the ten items
of the section 12 row 'Deferred after the upstream merge' (2026-10-19), because the merge
review's own evidence (workflow wf_226b9df8-523) was lost. Nothing in the repository or in the
shared build tree was changed by the recon.

| Report | Items | Supporting files |
|---|---|---|
| `split.md` | (3) late function splitting | `split-files/`: `item3.diff` (the fix), `proposed-tests/` (validated test texts), `repro/` (inputs), `tools/` (one-TU private relink: `relink.ps1`, `mkrsp.py`) |
| `stop-after.md` | (4) `-stop-after=msil-isel` | `stop-after-files/`: `prototype.diff`, `compile.ps1`/`link.ps1`/`cmd.txt`/`llc.rsp` (private llc), `suite/` (validated `stop-after-isel.ll`), `run/dbg.*` (debug-info case), `small/` (reproducers), `sweep*.py`/`sweep*.txt` (corpus sweeps, MSIL and WebAssembly) |
| `eh-passes.md` | (5) LowerInvoke twice | `eh-passes-files/`: `tools/` (private llc variants, corpus, pass counts, IL diff), `invoke.ll`, `empty.ll`, `ehcount-current.txt`, `ildiff.txt` |
| `hooks.md` | (6), (7) upstream hooks | `hooks-artifacts/`: draft manifest `upstream-hooks.txt`, `check_manifest.py`, `proto-hooks-compile.ps1`, `configure-hooks.ps1`, `gen_litroot.py` (private lit root), `curated.py`, `curated-paths.txt`, `run_curated.py`, `run_groups.py`, `cc1as-unsupported.s`, timings |
| `legs.md` | (1), (2), (8), (9), (10) | `legs-proto/`: `aarch64-aggregate-abi.c`, `mllvm.c`, `check-action-pins.py`, `lit-legs.*.utc.diff`, `lit.cfg.py.pe-machine.diff`, `aarch64-agg-probe.c` |

The reports name the places where the files lived during the recon. They map to this
directory as follows:

- `D:\llvm-msil\msil\orchestration\M11\recon\<x>` is `<x>` here.
- `$S` or `...\scratchpad\m11\stopafter` in `stop-after.md` is `stop-after-files/` (only the
  files listed above were kept; private llc binaries and lit outputs were not).
- `...\scratchpad\m11\eh-passes` in `eh-passes.md` is `eh-passes-files/` (same rule).
- `<scratch>` in any file stands for the recon session's scratch directory on the development
  host (machine-specific paths were replaced before the evidence was committed).
- Other scratch paths (private builds, lit roots, the `split` and `legs` scratch trees) were
  not kept; the reports quote the results.

The private hooks build tree that `hooks.md` describes is `C:\llvm-msil-hooks-build` on the
development host (not in the repository).
