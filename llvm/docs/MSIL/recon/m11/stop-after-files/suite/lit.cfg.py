import os
import lit.formats
config.name = 'msil-stop-after-scratch'
config.test_format = lit.formats.ShTest(execute_external=False)
config.suffixes = ['.ll']
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = lit_config.params['out']
config.environment['PATH'] = os.pathsep.join(
    [lit_config.params['llc_dir'], 'D:/llvm-msil/build/bin', config.environment.get('PATH', '')])
ilasm = r'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe'
if os.path.exists(ilasm):
    config.available_features.add('ilasm')
    config.substitutions.append(('%ilasm-verify', '"%s" -nologo -quiet -dll -pe64 -x64' % ilasm))
