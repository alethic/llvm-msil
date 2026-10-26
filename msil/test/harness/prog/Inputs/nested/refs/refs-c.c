// Run in nested lits by ../../../../ref-flags.test (M21): %{msil_c_O2} as a
// one-source program under msil_pipeline=obj, built by msil-prog.py with the
// stand-ins of ../../prog-cc.py (which read the value below), whose msil-ld
// link gets %{msil_ldflags}, then verified and run.
// RUN: %{msil_c_O2}
// prog-value: 9
