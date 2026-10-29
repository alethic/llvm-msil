/* What a C program on x86_64-unknown-linux-gnu-msil sees of its own name and command line. */
typedef struct _IO_FILE FILE;
FILE *fopen(const char *, const char *);
unsigned long fread(void *, unsigned long, unsigned long, FILE *);
int fclose(FILE *);
int puts(const char *);
extern char *program_invocation_name, *program_invocation_short_name;
static char *g_argv0;
static void ctor(int argc, char **argv, char **envp) { (void)argc; (void)envp; g_argv0 = argv[0]; }
__attribute__((section(".init_array"), used)) static void (*p_ctor)(int, char **, char **) = ctor;
int main(int argc, char **argv) {
  puts("-- main argv[0]:"); puts(argv[0]);
  puts("-- .init_array ctor argv[0]:"); puts(g_argv0 ? g_argv0 : "(null)");
  puts("-- program_invocation_name / short:"); puts(program_invocation_name); puts(program_invocation_short_name);
  puts("-- /proc/self/cmdline:");
  FILE *f = fopen("/proc/self/cmdline", "r"); char buf[512]; unsigned long n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
  if (f) fclose(f);
  for (unsigned long i = 0; i < n; ++i) if (!buf[i]) buf[i] = ' ';
  buf[n] = 0; puts(buf);
  return 0;
}
