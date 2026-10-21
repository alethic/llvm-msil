# Scratch lit config (recon driver-toolchain): runs the proposed clang/test/Driver/msil-*.c
# tests against the prototype clang. Inputs/ is a junction to clang/test/Driver/Inputs.
import os
import lit.formats
config.name = 'msil-driver-proto'
config.test_format = lit.formats.ShTest(False)
config.suffixes = ['.c']
config.excludes = ['Inputs', 'out']
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.test_source_root, 'out')
proto = 'C:/llvm-msil-scratch/p2/driver-toolchain/proto/bin/clang.exe'
config.substitutions.append(('%clang_cl', '"%s" --driver-mode=cl' % proto))
config.substitutions.append(('%clang', '"%s"' % proto))
config.environment['PATH'] = os.pathsep.join(['D:/llvm-msil/build/bin', os.environ.get('PATH', '')])
for v in ('INCLUDE', 'LIB', 'SYSTEMROOT', 'TEMP', 'TMP'):
    if v in os.environ:
        config.environment[v] = os.environ[v]
