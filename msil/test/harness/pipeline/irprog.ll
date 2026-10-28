; %{msil_ll} at every llc level among msil_opt_levels (plan 5.5), with the
; stand-in for llc of lit.local.cfg: each level runs the same legs as
; %{msil_c} (prog.c), and the native-oracle leg builds the IR without
; %{msil_cflags}. The checks after the composite show that every leg of every
; level ran.
; The asm expansion, the asm oracle from M23 (harness/pipeline-obj pins obj).
; REQUIRES: msil-pipeline=asm
; RUN: rm -f %t.*.out
; RUN: %{msil_ll}
; RUN: %if msil-ll-opt-O0 %{ %{harness_check} equals %t.O0.out "[exit 13]" %}
; RUN: %if msil-ll-opt-O1 %{ %{harness_check} equals %t.O1.out "[exit 13]" %}
; RUN: %if msil-ll-opt-O2 %{ %{harness_check} equals %t.O2.out "[exit 13]" %}
; RUN: %if msil-ll-opt-O3 %{ %{harness_check} equals %t.O3.out "[exit 13]" %}
; RUN: %if msil-native-oracle && msil-ll-opt-O0 %{ %{harness_check} equals %t.O0.native.out "[exit 13]" %}
; RUN: %if msil-native-oracle && msil-ll-opt-O1 %{ %{harness_check} equals %t.O1.native.out "[exit 13]" %}
; RUN: %if msil-native-oracle && msil-ll-opt-O2 %{ %{harness_check} equals %t.O2.native.out "[exit 13]" %}
; RUN: %if msil-native-oracle && msil-ll-opt-O3 %{ %{harness_check} equals %t.O3.native.out "[exit 13]" %}
; RUN: %if msil-run-netfx && msil-ll-opt-O0 %{ %{harness_check} equals %t.O0.netfx.out "[exit 13]" %}
; RUN: %if msil-run-netfx && msil-ll-opt-O1 %{ %{harness_check} equals %t.O1.netfx.out "[exit 13]" %}
; RUN: %if msil-run-netfx && msil-ll-opt-O2 %{ %{harness_check} equals %t.O2.netfx.out "[exit 13]" %}
; RUN: %if msil-run-netfx && msil-ll-opt-O3 %{ %{harness_check} equals %t.O3.netfx.out "[exit 13]" %}

define i32 @main() {
  ret i32 13
}
