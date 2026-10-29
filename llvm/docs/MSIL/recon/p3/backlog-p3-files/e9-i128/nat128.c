/* SysV x86-64: __int128 is two INTEGER eightbytes (RDI:RSI in, RAX:RDX out), but 16-byte aligned
   in memory, so on the stack it is placed at a 16-byte boundary, unlike struct {long long a, b;}. */
typedef __int128 i128;
i128 mul3p1(i128 a) { return a * 3 + 1; }
/* five GPRs used, only R9 left: an __int128 never splits, it goes to the stack */
i128 split5(long a1, long a2, long a3, long a4, long a5, i128 x) { return x + a1 + a2 + a3 + a4 + a5; }
/* six GPRs used, one 8-byte stack argument first: the __int128 goes to [rsp+16+8], not [rsp+8+8] */
i128 stk7(long a1, long a2, long a3, long a4, long a5, long a6, long s7, i128 x) { return x + s7; }
/* the same with struct {i64, i64} for comparison */
struct p { long long a, b; };
struct p stk7s(long a1, long a2, long a3, long a4, long a5, long a6, long s7, struct p x) { x.a += s7; return x; }
