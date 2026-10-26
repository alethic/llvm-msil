// Run in nested lits by ../../../../skip-peverify.test (M21): %{msil_c_O2}
// as a one-source program under msil_pipeline=obj, built by msil-prog.py with
// the stand-ins of ../../prog-cc.py (which read the value below), verified
// (with --skip-peverify and the pack as --runtime-dir under
// msil_references=net8) and run. A file of its own, so that its Output/ files
// are never those of refs-c.c, which other tests run at the same time.
// RUN: %{msil_c_O2}
// prog-value: 11
