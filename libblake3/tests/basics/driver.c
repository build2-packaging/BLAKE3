#include <stdint.h>
#include <string.h> /* strcmp(), memcmp() */

#include <blake3.h>

#undef NDEBUG
#include <assert.h>

int
main ()
{
  /* Make sure the library we have linked is the one whose header we have
   * included. In the installed case this catches picking up some other
   * version from the system.
   */
  assert (strcmp (blake3_version (), BLAKE3_VERSION_STRING) == 0);

  /* Hash the empty input, going through the hasher's entire initialize,
   * update, and finalize sequence. See test_vectors.json in upstream for
   * the expected value.
   */
  {
    static const uint8_t e[BLAKE3_OUT_LEN] = {
      0xaf, 0x13, 0x49, 0xb9, 0xf5, 0xf9, 0xa1, 0xa6,
      0xa0, 0x40, 0x4d, 0xea, 0x36, 0xdc, 0xc9, 0x49,
      0x9b, 0xcb, 0x25, 0xc9, 0xad, 0xc1, 0x12, 0xb7,
      0xcc, 0x9a, 0x93, 0xca, 0xe4, 0x1f, 0x32, 0x62};

    blake3_hasher h;
    blake3_hasher_init (&h);
    blake3_hasher_update (&h, "", 0);

    uint8_t r[BLAKE3_OUT_LEN];
    blake3_hasher_finalize (&h, r, sizeof (r));

    assert (memcmp (r, e, sizeof (r)) == 0);
  }

  return 0;
}
