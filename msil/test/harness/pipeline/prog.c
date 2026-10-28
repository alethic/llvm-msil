// %{msil_c} at every level of msil_opt_levels (plan 5.5), with the stand-in
// compiler of lit.local.cfg: each level compiles, assembles, verifies,
// force-JITs, runs prog.il.in from a private directory and compares the
// transcript with prog.expected. Where they can run, the native-oracle leg
// (the stand-in's .NET Framework executable) and the .NET Framework leg
// compare with the same file. The checks after the composite show that every
// leg of every level ran; the transcripts of an earlier run are removed
// first.
// The asm expansion (-S plus ilasm), the asm oracle from M23: under the
// default obj pipeline harness/pipeline-obj pins %{msil_c}.
// REQUIRES: msil-pipeline=asm
// RUN: rm -f %t.*.out
// RUN: %{msil_c}
// RUN: %if msil-opt-O0 %{ %{harness_check} equals %t.O0.out "[exit 7]" %}
// RUN: %if msil-opt-O1 %{ %{harness_check} equals %t.O1.out "[exit 7]" %}
// RUN: %if msil-opt-O2 %{ %{harness_check} equals %t.O2.out "[exit 7]" %}
// RUN: %if msil-opt-O3 %{ %{harness_check} equals %t.O3.out "[exit 7]" %}
// RUN: %if msil-opt-Os %{ %{harness_check} equals %t.Os.out "[exit 7]" %}
// RUN: %if msil-opt-Oz %{ %{harness_check} equals %t.Oz.out "[exit 7]" %}
// RUN: %if msil-native-oracle && msil-opt-O0 %{ %{harness_check} equals %t.O0.native.out "[exit 7]" %}
// RUN: %if msil-native-oracle && msil-opt-O1 %{ %{harness_check} equals %t.O1.native.out "[exit 7]" %}
// RUN: %if msil-native-oracle && msil-opt-O2 %{ %{harness_check} equals %t.O2.native.out "[exit 7]" %}
// RUN: %if msil-native-oracle && msil-opt-O3 %{ %{harness_check} equals %t.O3.native.out "[exit 7]" %}
// RUN: %if msil-native-oracle && msil-opt-Os %{ %{harness_check} equals %t.Os.native.out "[exit 7]" %}
// RUN: %if msil-native-oracle && msil-opt-Oz %{ %{harness_check} equals %t.Oz.native.out "[exit 7]" %}
// RUN: %if msil-run-netfx && msil-opt-O0 %{ %{harness_check} equals %t.O0.netfx.out "[exit 7]" %}
// RUN: %if msil-run-netfx && msil-opt-O1 %{ %{harness_check} equals %t.O1.netfx.out "[exit 7]" %}
// RUN: %if msil-run-netfx && msil-opt-O2 %{ %{harness_check} equals %t.O2.netfx.out "[exit 7]" %}
// RUN: %if msil-run-netfx && msil-opt-O3 %{ %{harness_check} equals %t.O3.netfx.out "[exit 7]" %}
// RUN: %if msil-run-netfx && msil-opt-Os %{ %{harness_check} equals %t.Os.netfx.out "[exit 7]" %}
// RUN: %if msil-run-netfx && msil-opt-Oz %{ %{harness_check} equals %t.Oz.netfx.out "[exit 7]" %}

#include "msil_test.h"

static int add3(int x) { return x + 3; }

int main(void) { return add3(4); }
