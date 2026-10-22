// Run in a nested lit by ../../../roundtrip.test with --param
// msil_pipeline=lto: %{msil_c} is then %{msil_prog} of this one source at
// every level of msil_opt_levels (../../prog-cc.py reads the value below),
// and with --param msil_roundtrip=1 its round-trip leg reassembles each
// level's <t>.<L>.d/prog.exe into <t>.<L>.rt.exe and runs it against
// rt-c.expected.
// REQUIRES: msil-pipeline=lto
// RUN: %{msil_c}
// prog-value: 9
