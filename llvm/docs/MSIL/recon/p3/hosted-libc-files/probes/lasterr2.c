/* hosted-libc recon: does a Win32 last-error value set by one P/Invoke survive until
   the next P/Invoke (GetLastError)? Also UCRT errno after _open. Fixed version of
   probe/lasterr.c (whose paths used "\n" and "\f" escapes by mistake). */
#include <windows.h>
#include <stdio.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
static const wchar_t *P1 = L"C:/no-such-dir-msil/f.txt";      /* ERROR_PATH_NOT_FOUND 3 */
static const wchar_t *P2 = L"C:/Windows/no-such-file-msil.txt"; /* ERROR_FILE_NOT_FOUND 2 */
__declspec(noinline) static DWORD probe_once(const wchar_t *p) {
  HANDLE h = CreateFileW(p, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
  DWORD e = GetLastError();
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  return e;
}
__declspec(noinline) static DWORD set_then_get(DWORD v) { SetLastError(v); return GetLastError(); }
int main(void) {
  DWORD first = probe_once(P1);
  DWORD sg = set_then_get(4242);
  int fd0 = _open("C:/no-such-dir-msil/f.txt", _O_RDONLY); int e0 = errno;
  printf("first: GetLastError=%lu (want 3) SetLastError(4242)->%lu errno=%d (want %d) fd=%d\n", first, sg, e0, ENOENT, fd0);
  int bad = 0, badSG = 0, badErrno = 0;
  for (int i = 0; i < 300000; i++) {
    DWORD e = probe_once(i & 1 ? P1 : P2);
    DWORD want = i & 1 ? ERROR_PATH_NOT_FOUND : ERROR_FILE_NOT_FOUND;
    if (e != want) { if (bad < 5) printf("i=%d GetLastError=%lu want %lu\n", i, e, want); bad++; }
    if (set_then_get((DWORD)i + 7) != (DWORD)i + 7) badSG++;
    errno = 0;
    int fd = _open("C:/no-such-dir-msil/f.txt", _O_RDONLY);
    if (fd >= 0 || errno != ENOENT) badErrno++;
  }
  printf("mismatches of 300000: GetLastError %d SetLastError/GetLastError %d errno %d\n", bad, badSG, badErrno);
  return 0;
}
