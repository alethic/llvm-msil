/* msil-ld.exe launcher for the stand-in: runs
 *   <python> -I <dir of this exe>\msil-ld-standin.py <original arguments>
 * where <python> is %MSIL_STANDIN_PYTHON% or "python".
 * Built with: clang-cl /nologo /O2 /D_CRT_SECURE_NO_WARNINGS standin-launcher.c /Fe:msil-ld.exe */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

int wmain(void) {
  wchar_t exe[MAX_PATH], py[MAX_PATH] = L"python", cmd[32768];
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  wchar_t *slash = wcsrchr(exe, L'\\');
  if (slash) *slash = 0;
  GetEnvironmentVariableW(L"MSIL_STANDIN_PYTHON", py, MAX_PATH);
  /* skip argv[0] in the raw command line */
  wchar_t *p = GetCommandLineW();
  if (*p == L'"') { ++p; while (*p && *p != L'"') ++p; if (*p) ++p; }
  else { while (*p && *p != L' ' && *p != L'\t') ++p; }
  _snwprintf(cmd, 32768, L"\"%s\" -I \"%s\\msil-ld-standin.py\"%s", py, exe, p);
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi;
  if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
    fwprintf(stderr, L"msil-ld launcher: cannot start %s\n", py);
    return 1;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  return (int)code;
}
