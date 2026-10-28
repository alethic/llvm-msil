/* Runs CoreMark (core_main.c, compiled with -Dmain=coremark_main into the
   archive) twice and reports what CoreMark's own validation found. main's
   return value is always 0 (coremark.h's MAIN_RETURN_VAL), so the results
   come from the port (core_portme.h): context 0's CRCs and CoreMark's error
   count, which portable_fini records at the end of each run.
   1. The performance run: seeds 0, 0, 0x66, ITERATIONS iterations and 2000
      bytes of data (CoreMark's '2K performance run' parameters). CoreMark
      compares the CRCs of its list, matrix and state benchmarks with the
      known values of these parameters; its error count is the number of
      mismatches (0 when it validates the run).
   2. The collision run, a negative check: the seeds 0x4003, 1, 0x66 give the
      performance run's seed CRC (CoreMark's crc16 of the seeds and the size,
      0xe9f5), so CoreMark compares their different CRCs with the same known
      values and must count mismatches. One iteration suffices, because
      CoreMark validates the first iteration's CRCs.
   Each run prints its CRCs and its error count. The exit status is the
   performance run's error count (255 if negative or above 255), or 255 when
   the collision run counts no mismatch, because then the driver does not
   see CoreMark's result. */
#undef main /* every translation unit is compiled with -Dmain=coremark_main */
#include "core_portme.h"
#include "sup.h"
int coremark_main(int argc, char *argv[]);

static void put_crc(const char *label, ee_u16 crc) {
  unsigned char bytes[2] = {(unsigned char)(crc >> 8), (unsigned char)crc};
  sup_put_hex(label, bytes, sizeof bytes);
}

/* One run of CoreMark; prints its CRCs and error count under labels[0..4]
   and returns the error count. */
static int run(const char *const labels[5]) {
  static char arg0[] = "coremark";
  char *argv[] = {arg0, 0};
  coremark_main(1, argv);
  put_crc(labels[0], portable_result.crclist);
  put_crc(labels[1], portable_result.crcmatrix);
  put_crc(labels[2], portable_result.crcstate);
  put_crc(labels[3], portable_result.crcfinal);
  sup_put_i(labels[4], portable_result.errors);
  return portable_result.errors;
}

int main(void) {
  static const char *const performance[5] = {
      "coremark crclist", "coremark crcmatrix", "coremark crcstate",
      "coremark crcfinal", "coremark errors"};
  static const char *const collision[5] = {
      "collision crclist", "collision crcmatrix", "collision crcstate",
      "collision crcfinal", "collision errors"};
  int errors = run(performance);
  portable_seeds(0x4003, 1, 0x66, 1);
  if (run(collision) <= 0)
    return 255;
  return errors >= 0 && errors <= 255 ? errors : 255;
}
