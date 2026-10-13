; The IR source exit-range.test hands to update-expected.py, shaped like an
; execution test. The fake toolchain there (Inputs/fake-native-*.il) checks
; the command line update-expected.py builds for IR and otherwise ignores the
; file, so the native result is whatever that fake program returns.
; RUN: %{msil_ll}
define i32 @main() {
  ret i32 300
}
