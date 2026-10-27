#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>  /* malloc(), realloc(), free(), exit() */
#include <string.h>  /* strlen(), strcmp(), strncmp() */
#include <stdbool.h>

#include <blake3.h>

#undef NDEBUG
#include <assert.h>

/* The test vectors file (test_vectors.json in upstream) as we need it.
 */
struct test_case
{
  size_t input_len;

  /* Hex-encoded extended outputs of each mode.
   */
  char* hash;
  char* keyed_hash;
  char* derive_key;
};

struct test_vectors
{
  char* key;
  char* context_string;

  struct test_case* cases;
  size_t cases_size;
};

/* JSON parser for the subset of the format that the test vectors file
 * uses. Note that it doesn't attempt to recover from errors: it reports
 * the first one and exits.
 */
struct parser
{
  const char* name; /* Input name for diagnostics. */
  const char* b;    /* Input begin. */
  const char* p;    /* Current position. */
  const char* e;    /* Input end. */
};

static void*
allocate (size_t n)
{
  void* r = malloc (n != 0 ? n : 1);
  assert (r != NULL);
  return r;
}

/* Report that we expected the specified thing at the current position and
 * exit.
 */
static void
fail (const struct parser* p, const char* what)
{
  unsigned long l = 1, c = 1;

  for (const char* i = p->b; i != p->p; ++i)
  {
    if (*i == '\n')
    {
      ++l;
      c = 1;
    }
    else
      ++c;
  }

  unsigned char x = (p->p != p->e ? (unsigned char) *p->p : 0);

  if (p->p != p->e && x >= 0x20 && x < 0x7f)
    fprintf (stderr,
             "%s:%lu:%lu: error: expected %s instead of '%c'\n",
             p->name, l, c, what, *p->p);
  else if (p->p != p->e)
    fprintf (stderr,
             "%s:%lu:%lu: error: expected %s instead of '\\x%02x'\n",
             p->name, l, c, what, x);
  else
    fprintf (stderr,
             "%s:%lu:%lu: error: expected %s instead of end of input\n",
             p->name, l, c, what);

  exit (1);
}

static void
skip_spaces (struct parser* p)
{
  for (; p->p != p->e; ++p->p)
  {
    char c = *p->p;
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
      break;
  }
}

/* Skip spaces and return true if the next character is the specified one.
 */
static bool
peek (struct parser* p, char c)
{
  skip_spaces (p);
  return p->p != p->e && *p->p == c;
}

/* As above but also consume the character if it matches.
 */
static bool
try_next (struct parser* p, char c)
{
  if (!peek (p, c))
    return false;

  ++p->p;
  return true;
}

static void
next (struct parser* p, char c, const char* what)
{
  if (!try_next (p, c))
    fail (p, what);
}

static int
hex_value (char c)
{
  return (c >= '0' && c <= '9' ? c - '0'      :
          c >= 'a' && c <= 'f' ? c - 'a' + 10 :
          c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1);
}

/* Parse a string, returning its decoded value which the caller should
 * free.
 *
 * We decode \uXXXX escapes into UTF-8 except for surrogates, which the
 * test vectors have no use for, and for NUL, which C strings cannot
 * represent.
 */
static char*
parse_string (struct parser* p)
{
  skip_spaces (p);

  if (p->p == p->e || *p->p != '"')
    fail (p, "string");

  ++p->p;

  /* Find the closing quote first, which gives us an upper bound on the
   * decoded size since no escape sequence decodes into more characters
   * than it occupies.
   */
  const char* b = p->p;

  for (; p->p != p->e && *p->p != '"'; ++p->p)
  {
    if (*p->p == '\\' && p->p + 1 != p->e)
      ++p->p;
  }

  if (p->p == p->e)
    fail (p, "closing '\"'");

  const char* e = p->p++;

  char* r = allocate ((size_t) (e - b) + 1);
  char* o = r;

  for (const char* i = b; i != e; )
  {
    char c = *i;

    if ((unsigned char) c < 0x20)
    {
      p->p = i;
      fail (p, "printable character");
    }

    if (c != '\\')
    {
      *o++ = c;
      ++i;
      continue;
    }

    switch (*++i)
    {
      case '"':  *o++ = '"';  break;
      case '\\': *o++ = '\\'; break;
      case '/':  *o++ = '/';  break;
      case 'b':  *o++ = '\b'; break;
      case 'f':  *o++ = '\f'; break;
      case 'n':  *o++ = '\n'; break;
      case 'r':  *o++ = '\r'; break;
      case 't':  *o++ = '\t'; break;
      case 'u':
      {
        unsigned long v = 0;

        for (int n = 0; n != 4; ++n)
        {
          int d = (i + 1 != e ? hex_value (i[1]) : -1);

          if (d == -1)
          {
            p->p = i + 1;
            fail (p, "hexadecimal digit");
          }

          v = v * 16 + (unsigned long) d;
          ++i;
        }

        if (v == 0 || (v >= 0xd800 && v <= 0xdfff))
        {
          p->p = i - 3;
          fail (p, "non-NUL, non-surrogate code point");
        }

        if (v < 0x80)
          *o++ = (char) v;
        else if (v < 0x800)
        {
          *o++ = (char) (0xc0 | (v >> 6));
          *o++ = (char) (0x80 | (v & 0x3f));
        }
        else
        {
          *o++ = (char) (0xe0 | (v >> 12));
          *o++ = (char) (0x80 | ((v >> 6) & 0x3f));
          *o++ = (char) (0x80 | (v & 0x3f));
        }

        break;
      }
      default:
      {
        p->p = i;
        fail (p, "escape sequence");
      }
    }

    ++i;
  }

  *o = '\0';
  return r;
}

/* Parse a string that holds a hex-encoded output, which must be at least
 * the default output length. Note that we only accept lowercase digits
 * since we compare the outputs as strings.
 */
static char*
parse_output (struct parser* p)
{
  skip_spaces (p);
  const char* b = p->p;

  char* r = parse_string (p);
  size_t n = strlen (r);

  bool valid = (n % 2 == 0 && n >= 2 * BLAKE3_OUT_LEN);

  for (size_t i = 0; valid && i != n; ++i)
    valid = ((r[i] >= '0' && r[i] <= '9') || (r[i] >= 'a' && r[i] <= 'f'));

  if (!valid)
  {
    p->p = b;
    fail (p, "lowercase hex-encoded output of at least 32 bytes");
  }

  return r;
}

static size_t
parse_size (struct parser* p)
{
  skip_spaces (p);

  if (p->p == p->e || *p->p < '0' || *p->p > '9')
    fail (p, "non-negative integer");

  size_t r = 0;

  for (; p->p != p->e && *p->p >= '0' && *p->p <= '9'; ++p->p)
  {
    size_t d = (size_t) (*p->p - '0');

    if (r > (SIZE_MAX - d) / 10)
      fail (p, "smaller integer");

    r = r * 10 + d;
  }

  return r;
}

/* Skip a value of any type, for example, the _comment member.
 */
static void
skip_value (struct parser* p)
{
  skip_spaces (p);

  if (p->p == p->e)
    fail (p, "value");

  switch (*p->p)
  {
    case '"':
    {
      free (parse_string (p));
      break;
    }
    case '{':
    case '[':
    {
      char c = (*p->p++ == '{' ? '}' : ']');

      if (!peek (p, c))
      {
        do
        {
          if (c == '}')
          {
            free (parse_string (p));
            next (p, ':', "':'");
          }

          skip_value (p);
        }
        while (try_next (p, ','));
      }

      next (p, c, (c == '}' ? "',' or '}'" : "',' or ']'"));
      break;
    }
    default:
    {
      /* Numbers and the true, false, and null literals.
       */
      const char* b = p->p;

      for (; p->p != p->e; ++p->p)
      {
        char c = *p->p;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              c == '-' || c == '+' || c == '.' || c == 'E'))
          break;
      }

      if (p->p == b)
        fail (p, "value");
    }
  }
}

static void
parse_case (struct parser* p, struct test_case* r)
{
  r->input_len = SIZE_MAX;
  r->hash = r->keyed_hash = r->derive_key = NULL;

  next (p, '{', "'{'");

  if (!peek (p, '}'))
  {
    do
    {
      char* n = parse_string (p);
      next (p, ':', "':'");

      if      (strcmp (n, "input_len")  == 0) r->input_len = parse_size (p);
      else if (strcmp (n, "hash")       == 0) r->hash = parse_output (p);
      else if (strcmp (n, "keyed_hash") == 0) r->keyed_hash = parse_output (p);
      else if (strcmp (n, "derive_key") == 0) r->derive_key = parse_output (p);
      else                                    skip_value (p);

      free (n);
    }
    while (try_next (p, ','));
  }

  if (!peek (p, '}'))
    fail (p, "',' or '}'");

  if (r->input_len == SIZE_MAX) fail (p, "'input_len' member");
  if (r->hash == NULL)          fail (p, "'hash' member");
  if (r->keyed_hash == NULL)    fail (p, "'keyed_hash' member");
  if (r->derive_key == NULL)    fail (p, "'derive_key' member");

  ++p->p;
}

static void
parse_cases (struct parser* p, struct test_vectors* r)
{
  size_t capacity = 0;

  /* Note that we require at least one case since an empty list most
   * likely means a broken file rather than nothing to test.
   */
  next (p, '[', "'['");

  do
  {
    if (r->cases_size == capacity)
    {
      capacity = (capacity != 0 ? capacity * 2 : 64);
      r->cases = realloc (r->cases, capacity * sizeof (struct test_case));
      assert (r->cases != NULL);
    }

    parse_case (p, &r->cases[r->cases_size++]);
  }
  while (try_next (p, ','));

  next (p, ']', "',' or ']'");
}

static void
parse_vectors (struct parser* p, struct test_vectors* r)
{
  r->key = r->context_string = NULL;
  r->cases = NULL;
  r->cases_size = 0;

  next (p, '{', "'{'");

  if (!peek (p, '}'))
  {
    do
    {
      char* n = parse_string (p);
      next (p, ':', "':'");

      if (strcmp (n, "key") == 0)
      {
        skip_spaces (p);
        const char* b = p->p;

        r->key = parse_string (p);

        if (strlen (r->key) != BLAKE3_KEY_LEN)
        {
          p->p = b;
          fail (p, "32-byte key");
        }
      }
      else if (strcmp (n, "context_string") == 0)
        r->context_string = parse_string (p);
      else if (strcmp (n, "cases") == 0)
        parse_cases (p, r);
      else
        skip_value (p);

      free (n);
    }
    while (try_next (p, ','));
  }

  if (!peek (p, '}'))
    fail (p, "',' or '}'");

  if (r->key == NULL)            fail (p, "'key' member");
  if (r->context_string == NULL) fail (p, "'context_string' member");
  if (r->cases == NULL)          fail (p, "'cases' member");

  ++p->p;

  skip_spaces (p);

  if (p->p != p->e)
    fail (p, "end of input");
}

/* Compare the hasher's output to the expected hex-encoded extended output
 * as well as to its default-length prefix. Return the number of mismatches,
 * reporting each to stderr.
 */
static unsigned
check (const blake3_hasher* h,
       const char* mode,
       size_t input_len,
       const char* expected)
{
  unsigned r = 0;

  size_t n = strlen (expected) / 2;

  uint8_t* o = allocate (n);
  char* s = allocate (2 * n + 1);

  /* Check the extended output first and then the default-length one,
   * which must match the expected output's prefix.
   */
  static const char digits[] = "0123456789abcdef";

  for (int x = 0; x != 2; ++x)
  {
    bool extended = (x == 0);
    size_t m = (extended ? n : BLAKE3_OUT_LEN);

    blake3_hasher_finalize (h, o, m);

    for (size_t i = 0; i != m; ++i)
    {
      s[2 * i]     = digits[o[i] >> 4];
      s[2 * i + 1] = digits[o[i] & 0x0f];
    }

    s[2 * m] = '\0';

    if (strncmp (s, expected, 2 * m) != 0)
    {
      fprintf (stderr,
               "error: %s%s(%lu): expected %.*s instead of %s\n",
               mode,
               (extended ? "_xof" : ""),
               (unsigned long) input_len,
               (int) (2 * m),
               expected,
               s);
      ++r;
    }
  }

  free (s);
  free (o);

  return r;
}

/* Usage: argv[0]
 *
 * Read the test vectors in the test_vectors.json format from stdin and
 * check that the library reproduces them, the same way as test.py in
 * upstream does it. Specifically, for each case, fill the input with the
 * repeating sequence of 251 bytes (0, 1, ..., 250, 0, 1, ...) and verify
 * the extended output as well as its default-length prefix in each of the
 * hash, keyed hash, and derive key modes.
 *
 * Report each mismatch to stderr and exit with non-zero status if there
 * are any.
 */
int
main (void)
{
  /* Read the input.
   */
  char* b = NULL;
  size_t n = 0;

  for (size_t c = 0;; )
  {
    if (n == c)
    {
      c = (c != 0 ? c * 2 : 65536);
      b = realloc (b, c);
      assert (b != NULL);
    }

    size_t m = fread (b + n, 1, c - n, stdin);

    if (m == 0)
      break;

    n += m;
  }

  if (ferror (stdin))
  {
    fprintf (stderr, "error: unable to read stdin\n");
    return 1;
  }

  /* Parse the test vectors.
   */
  struct test_vectors v;
  {
    struct parser p = {"<stdin>", b, b, b + n};
    parse_vectors (&p, &v);
  }

  free (b);

  /* Check each case in each mode.
   */
  unsigned r = 0;

  for (size_t i = 0; i != v.cases_size; ++i)
  {
    const struct test_case* c = &v.cases[i];

    /* Use 251 since it is the largest prime less than 256, which makes it
     * unlikely that swapping any two adjacent blocks or chunks gives the
     * same result.
     */
    uint8_t* in = allocate (c->input_len);

    for (size_t j = 0; j != c->input_len; ++j)
      in[j] = (uint8_t) (j % 251);

    blake3_hasher h;

    blake3_hasher_init (&h);
    blake3_hasher_update (&h, in, c->input_len);
    r += check (&h, "hash", c->input_len, c->hash);

    blake3_hasher_init_keyed (&h, (const uint8_t*) v.key);
    blake3_hasher_update (&h, in, c->input_len);
    r += check (&h, "keyed_hash", c->input_len, c->keyed_hash);

    blake3_hasher_init_derive_key (&h, v.context_string);
    blake3_hasher_update (&h, in, c->input_len);
    r += check (&h, "derive_key", c->input_len, c->derive_key);

    free (in);
  }

  for (size_t i = 0; i != v.cases_size; ++i)
  {
    free (v.cases[i].hash);
    free (v.cases[i].keyed_hash);
    free (v.cases[i].derive_key);
  }

  free (v.cases);
  free (v.context_string);
  free (v.key);

  return r == 0 ? 0 : 1;
}
