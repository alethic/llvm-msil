// Run in nested lits by ../../../o6-skip.test (M21): %{msil_c_O2} as a
// one-source program under msil_pipeline=obj with msil_equiv=1, built by
// msil-prog.py with the stand-ins of ../../prog-cc.py (which read the value
// below), verified with O6 against its -S build, run, and compared with that
// build canonically (O3). A file of its own, so that its Output/ files are
// never those of refs-c.c, which other tests run at the same time.
// RUN: %{msil_c_O2}
// prog-value: 13
