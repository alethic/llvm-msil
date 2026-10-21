/* p8.c: a constructor registered through a named section, a common C idiom:
   ELF '.init_array' and MSVC '.CRT$XCU' (both read by the native startup code). */
int counter = 1;
static void init_fn(void) { counter = 40; }
#if defined(_WIN32)
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) __attribute__((used)) static void (*p_init)(void) = init_fn;
#else
__attribute__((used, section(".init_array"))) static void (*p_init)(void) = init_fn;
#endif
int main(void) { return counter + 2; }   /* native: 42 */
