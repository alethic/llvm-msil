# MSIL recon reports

Recon reports and verified experiments behind `../ImplementationPlan.md`: the Phase 1 reports
in this folder with their experiments in `artifacts/`, and the M11 recon in `m11/`.

Path placeholders. The reports and artifacts quote the paths the recon agents used on the
development host; machine-specific parts were replaced before they were published:

- `<scratch>` is the scratch directory of the orchestrating session that ran the recon. It was
  temporary and is gone; only the files copied into `artifacts/` and `m11/` were kept.
- `%LOCALAPPDATA%` and `%USERPROFILE%` stand for the developer's Windows profile folders (for
  example the per-user Python installation under `%LOCALAPPDATA%/Python/`).
