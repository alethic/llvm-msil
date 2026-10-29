#include <emmintrin.h>
int f_movemask(const char *p, char c) { __m128i v = _mm_loadu_si128((const __m128i *)p); return _mm_movemask_epi8(_mm_cmpeq_epi8(v, _mm_set1_epi8(c))); }
__m128i f_sad(__m128i a, __m128i b) { return _mm_sad_epu8(a, b); }
__m128i f_packus(__m128i a, __m128i b) { return _mm_packus_epi16(a, b); }
__m128i f_madd(__m128i a, __m128i b) { return _mm_madd_epi16(a, b); }
__m128i f_cvtps(__m128 a) { return _mm_cvtps_epi32(a); }
__m128i f_min(__m128i a, __m128i b) { return _mm_min_epu8(a, b); }
__m128i f_shuf(__m128i a) { return _mm_shuffle_epi32(a, 0x1b); }
__m128i f_srli(__m128i a) { return _mm_srli_si128(a, 4); }
__m128i f_mul(__m128i a, __m128i b) { return _mm_mul_epu32(a, b); }
__m128i f_slli(__m128i a, int n) { return _mm_slli_epi32(a, n); }
__m128d f_sqrt(__m128d a) { return _mm_sqrt_pd(a); }
