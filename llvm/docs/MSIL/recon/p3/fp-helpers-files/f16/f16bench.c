// Conversion throughput: 2^28 float -> half and 2^28 half -> float, little else in the loop.
int puts(const char *);
static void hx(unsigned v){char b[12];for(int i=0;i<8;i++)b[i]="0123456789abcdef"[(v>>(28-4*i))&15];b[8]=0;puts(b);}
int main(int argc, char **argv) {
  const char *m = argc > 1 ? argv[1] : "tx";
  float x = 0.37f * argc;
  unsigned acc = 0;
  if (m[0] == 't') {          // truncation f32 -> f16
    for (int i = 0; i < (1 << 28); ++i) {
      _Float16 h = (_Float16)x;
      unsigned short u; __builtin_memcpy(&u, &h, 2);
      acc += u;
      x = x * 1.0001f + 0.5f;
      if (x > 60000.0f) x = -60000.0f;
    }
  } else {                    // extension f16 -> f32
    float s = 0;
    for (int i = 0; i < (1 << 28); ++i) {
      unsigned short u = (unsigned short)(i * 40503u >> 3);
      _Float16 h; __builtin_memcpy(&h, &u, 2);
      float f = (float)h;
      s += (f - f == 0) ? f * 1e-3f : 0;
    }
    __builtin_memcpy(&acc, &s, 4);
  }
  hx(acc);
  return 0;
}
