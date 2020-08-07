/* Copyright JS Foundation and other contributors, http://js.foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ecma-bigint.h"
#include "ecma-big-uint.h"
#include "ecma-exceptions.h"
#include "ecma-helpers.h"
#include "lit-char-helpers.h"

#if ENABLED (JERRY_BUILTIN_BIGINT)

/**
 * Raise a not enough memory error
 *
 * @return ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
static ecma_value_t
ecma_bigint_raise_memory_error (void)
{
  return ecma_raise_range_error (ECMA_ERR_MSG ("Cannot allocate memory for a BigInt value"));
} /* ecma_bigint_raise_memory_error */

/**
 * Parse a string and create a BigInt value
 *
 * @return ecma BigInt value or a special value allowed by the option flags
 *         Returned value must be freed with ecma_free_value.
 */
ecma_value_t
ecma_bigint_parse_string (const lit_utf8_byte_t *string_p, /**< string represenation of the BigInt */
                          lit_utf8_size_t size, /**< string size */
                          uint32_t options) /**< ecma_bigint_parse_string_options_t option bits */
{
  ecma_bigint_digit_t radix = 10;
  uint32_t sign = (options & ECMA_BIGINT_PARSE_SET_NEGATIVE) ? ECMA_BIGINT_SIGN : 0;

  if (size >= 3 && string_p[0] == LIT_CHAR_0)
  {
    if (string_p[1] == LIT_CHAR_LOWERCASE_X || string_p[1] == LIT_CHAR_UPPERCASE_X)
    {
      radix = 16;
      string_p += 2;
      size -= 2;
    }
    else if (string_p[1] == LIT_CHAR_LOWERCASE_O || string_p[1] == LIT_CHAR_UPPERCASE_O)
    {
      radix = 8;
      string_p += 2;
      size -= 2;
    }
    else if (string_p[1] == LIT_CHAR_LOWERCASE_B || string_p[1] == LIT_CHAR_UPPERCASE_B)
    {
      radix = 2;
      string_p += 2;
      size -= 2;
    }
  }
  else if (size >= 2)
  {
    if (string_p[0] == LIT_CHAR_PLUS)
    {
      size--;
      string_p++;
    }
    else if (string_p[0] == LIT_CHAR_MINUS)
    {
      sign = ECMA_BIGINT_SIGN;
      size--;
      string_p++;
    }
  }
  else if (size == 0)
  {
    if (options & ECMA_BIGINT_PARSE_DISALLOW_SYNTAX_ERROR)
    {
      return ECMA_VALUE_FALSE;
    }
    return ecma_raise_syntax_error (ECMA_ERR_MSG ("BigInt cannot be constructed from empty string"));
  }

  const lit_utf8_byte_t *string_end_p = string_p + size;

  while (string_p < string_end_p && *string_p == LIT_CHAR_0)
  {
    string_p++;
  }

  ecma_extended_primitive_t *result_p = NULL;

  if (string_p == string_end_p)
  {
    return ECMA_BIGINT_ZERO;
  }

  do
  {
    ecma_bigint_digit_t digit = radix;

    if (*string_p >= LIT_CHAR_0 && *string_p <= LIT_CHAR_9)
    {
      digit = (ecma_bigint_digit_t) (*string_p - LIT_CHAR_0);
    }
    else
    {
      lit_utf8_byte_t character = (lit_utf8_byte_t) LEXER_TO_ASCII_LOWERCASE (*string_p);

      if (character >= LIT_CHAR_LOWERCASE_A && character <= LIT_CHAR_LOWERCASE_F)
      {
        digit = (ecma_bigint_digit_t) (character - (LIT_CHAR_LOWERCASE_A - 10));
      }
    }

    if (digit >= radix)
    {
      if (result_p != NULL)
      {
        ecma_deref_bigint (result_p);
      }

      if (options & ECMA_BIGINT_PARSE_DISALLOW_SYNTAX_ERROR)
      {
        return ECMA_VALUE_FALSE;
      }
      return ecma_raise_syntax_error (ECMA_ERR_MSG ("String cannot be converted to BigInt value"));
    }

    result_p = ecma_big_uint_mul_digit (result_p, radix, digit);

    if (JERRY_UNLIKELY (result_p == NULL))
    {
      break;
    }
  }
  while (++string_p < string_end_p);

  if (JERRY_UNLIKELY (result_p == NULL))
  {
    if (options & ECMA_BIGINT_PARSE_DISALLOW_MEMORY_ERROR)
    {
      return ECMA_VALUE_NULL;
    }
    return ecma_bigint_raise_memory_error ();
  }

  result_p->u.bigint_sign_and_size |= sign;
  return ecma_make_extended_primitive_value (result_p, ECMA_TYPE_BIGINT);
} /* ecma_bigint_parse_string */

/**
 * Parse a string value and create a BigInt value
 *
 * @return ecma BigInt value or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
ecma_value_t
ecma_bigint_parse_string_value (ecma_value_t string, /**< ecma string */
                                uint32_t options) /**< ecma_bigint_parse_string_options_t option bits */
{
  JERRY_ASSERT (ecma_is_value_string (string));

  ECMA_STRING_TO_UTF8_STRING (ecma_get_string_from_value (string), string_buffer_p, string_buffer_size);
  ecma_value_t result = ecma_bigint_parse_string (string_buffer_p, string_buffer_size, options);
  ECMA_FINALIZE_UTF8_STRING (string_buffer_p, string_buffer_size);

  return result;
} /* ecma_bigint_parse_string_value */

/**
 * Create a string representation for a BigInt value
 *
 * @return ecma string or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
ecma_string_t *
ecma_bigint_to_string (ecma_value_t value, /**< BigInt value */
                       ecma_bigint_digit_t radix) /**< conversion radix */
{
  JERRY_ASSERT (ecma_is_value_bigint (value));

  if (value == ECMA_BIGINT_ZERO)
  {
    return ecma_new_ecma_string_from_code_unit (LIT_CHAR_0);
  }

  uint32_t char_start_p, char_size_p;
  ecma_extended_primitive_t *bigint_p = ecma_get_extended_primitive_from_value (value);
  lit_utf8_byte_t *string_buffer_p = ecma_big_uint_to_string (bigint_p, radix, &char_start_p, &char_size_p);

  if (JERRY_UNLIKELY (string_buffer_p == NULL))
  {
    ecma_raise_range_error (ECMA_ERR_MSG ("Cannot allocate memory for a string representation of a BigInt value"));
    return NULL;
  }

  JERRY_ASSERT (char_start_p > 0);

  if (bigint_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN)
  {
    string_buffer_p[--char_start_p] = LIT_CHAR_MINUS;
  }

  ecma_string_t *string_p;
  string_p = ecma_new_ecma_string_from_utf8 (string_buffer_p + char_start_p, char_size_p - char_start_p);

  jmem_heap_free_block (string_buffer_p, char_size_p);
  return string_p;
} /* ecma_bigint_to_string */

/**
 * Get the size of zero digits from the result of ecma_bigint_number_to_digits
 */
#define ECMA_BIGINT_NUMBER_TO_DIGITS_GET_ZERO_SIZE(value) \
  (((value) & 0xffff) * (uint32_t) sizeof (ecma_bigint_digit_t))

/**
 * Get the number of digits from the result of ecma_bigint_number_to_digits
 */
#define ECMA_BIGINT_NUMBER_TO_DIGITS_GET_DIGITS(value) ((value) >> 20)

/**
 * Get the size of digits from the result of ecma_bigint_number_to_digits
 */
#define ECMA_BIGINT_NUMBER_TO_DIGITS_GET_DIGITS_SIZE(value) \
  (ECMA_BIGINT_NUMBER_TO_DIGITS_GET_DIGITS (value) * (uint32_t) sizeof (ecma_bigint_digit_t))

/**
 * Set number of digits in the result of ecma_bigint_number_to_digits
 */
#define ECMA_BIGINT_NUMBER_TO_DIGITS_SET_DIGITS(value) ((uint32_t) (value) << 20)

/**
 * This flag is set when the number passed to ecma_bigint_number_to_digits has fraction part
 */
#define ECMA_BIGINT_NUMBER_TO_DIGITS_HAS_FRACTION 0x10000

/**
 * Convert a number to maximum of 3 digits and left shift
 *
 * @return packed value, ECMA_BIGINT_NUMBER_TO_DIGITS* macros can be used to decode it
 */
static uint32_t
ecma_bigint_number_to_digits (ecma_number_t number, /**< ecma number */
                              ecma_bigint_digit_t *digits_p) /**< [out] BigInt digits */
{
  uint32_t biased_exp;
  uint64_t fraction;

  ecma_number_unpack (number, NULL, &biased_exp, &fraction);

  if (biased_exp == 0)
  {
    /* Number is zero. */
    return ECMA_BIGINT_NUMBER_TO_DIGITS_SET_DIGITS (0);
  }

  if (biased_exp < ((1 << (ECMA_NUMBER_BIASED_EXP_WIDTH - 1)) - 1))
  {
    /* Number is less than 1. */
    return ECMA_BIGINT_NUMBER_TO_DIGITS_SET_DIGITS (0) | ECMA_BIGINT_NUMBER_TO_DIGITS_HAS_FRACTION;
  }

  biased_exp -= ((1 << (ECMA_NUMBER_BIASED_EXP_WIDTH - 1)) - 1);
  fraction |= ((uint64_t) 1) << ECMA_NUMBER_FRACTION_WIDTH;

  if (biased_exp <= ECMA_NUMBER_FRACTION_WIDTH)
  {
    uint32_t has_fraction = 0;

    if (biased_exp < ECMA_NUMBER_FRACTION_WIDTH
        && (fraction << (biased_exp + ((8 * sizeof (uint64_t)) - ECMA_NUMBER_FRACTION_WIDTH))) != 0)
    {
      has_fraction |= ECMA_BIGINT_NUMBER_TO_DIGITS_HAS_FRACTION;
    }

    fraction >>= ECMA_NUMBER_FRACTION_WIDTH - biased_exp;
    digits_p[0] = (ecma_bigint_digit_t) fraction;

#if ENABLED (JERRY_NUMBER_TYPE_FLOAT64)
    digits_p[1] = (ecma_bigint_digit_t) (fraction >> (8 * sizeof (ecma_bigint_digit_t)));
    return ECMA_BIGINT_NUMBER_TO_DIGITS_SET_DIGITS (digits_p[1] == 0 ? 1 : 2) | has_fraction;
#else /* !ENABLED (JERRY_NUMBER_TYPE_FLOAT64) */
    return ECMA_BIGINT_NUMBER_TO_DIGITS_SET_DIGITS (1) | has_fraction;
#endif /* ENABLED (JERRY_NUMBER_TYPE_FLOAT64) */
  }

  digits_p[0] = (ecma_bigint_digit_t) fraction;
#if ENABLED (JERRY_NUMBER_TYPE_FLOAT64)
  digits_p[1] = (ecma_bigint_digit_t) (fraction >> (8 * sizeof (ecma_bigint_digit_t)));
#endif /* ENABLED (JERRY_NUMBER_TYPE_FLOAT64) */

  biased_exp -= ECMA_NUMBER_FRACTION_WIDTH;

  uint32_t shift_left = biased_exp & ((8 * sizeof (ecma_bigint_digit_t)) - 1);
  biased_exp = biased_exp >> ECMA_BIGINT_DIGIT_SHIFT;

  if (shift_left == 0)
  {
#if ENABLED (JERRY_NUMBER_TYPE_FLOAT64)
    return biased_exp | ECMA_BIGINT_NUMBER_TO_DIGITS_SET_DIGITS (2);
#else /* !ENABLED (JERRY_NUMBER_TYPE_FLOAT64) */
    return biased_exp | ECMA_BIGINT_NUMBER_TO_DIGITS_SET_DIGITS (1);
#endif /* ENABLED (JERRY_NUMBER_TYPE_FLOAT64) */
  }

  uint32_t shift_right = (1 << ECMA_BIGINT_DIGIT_SHIFT) - shift_left;

#if ENABLED (JERRY_NUMBER_TYPE_FLOAT64)
  digits_p[2] = digits_p[1] >> shift_right;
  digits_p[1] = (digits_p[1] << shift_left) | (digits_p[0] >> shift_right);
  digits_p[0] <<= shift_left;

  return biased_exp | ECMA_BIGINT_NUMBER_TO_DIGITS_SET_DIGITS (digits_p[2] == 0 ? 2 : 3);
#else /* !ENABLED (JERRY_NUMBER_TYPE_FLOAT64) */
  digits_p[1] = digits_p[0] >> shift_right;
  digits_p[0] <<= shift_left;

  return biased_exp | ECMA_BIGINT_NUMBER_TO_DIGITS_SET_DIGITS (digits_p[1] == 0 ? 1 : 2);
#endif /* ENABLED (JERRY_NUMBER_TYPE_FLOAT64) */
} /* ecma_bigint_number_to_digits */

/**
 * Convert an ecma number to BigInt value
 *
 * See also:
 *          ECMA-262 v11, 20.2.1.1.1
 *
 * @return ecma BigInt value or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
ecma_value_t
ecma_bigint_number_to_bigint (ecma_number_t number) /**< ecma number */
{
  if (!ecma_number_is_finite (number))
  {
    return ecma_raise_range_error (ECMA_ERR_MSG ("Infinity or NaN cannot be converted to BigInt"));
  }

  ecma_bigint_digit_t digits[3];
  uint32_t result = ecma_bigint_number_to_digits (number, digits);

  JERRY_ASSERT (ECMA_BIGINT_NUMBER_TO_DIGITS_GET_DIGITS (result) == 0
                || digits[ECMA_BIGINT_NUMBER_TO_DIGITS_GET_DIGITS (result) - 1] > 0);

  if (result & ECMA_BIGINT_NUMBER_TO_DIGITS_HAS_FRACTION)
  {
    return ecma_raise_range_error (ECMA_ERR_MSG ("Only integer numbers can be converted to BigInt"));
  }

  uint32_t digits_size = ECMA_BIGINT_NUMBER_TO_DIGITS_GET_DIGITS_SIZE (result);

  if (digits_size == 0)
  {
    return ECMA_BIGINT_ZERO;
  }

  uint32_t zero_size = ECMA_BIGINT_NUMBER_TO_DIGITS_GET_ZERO_SIZE (result);

  ecma_extended_primitive_t *result_p = ecma_bigint_create (digits_size + zero_size);

  if (JERRY_UNLIKELY (result_p == NULL))
  {
    return ecma_bigint_raise_memory_error ();
  }

  uint8_t *data_p = (uint8_t *) ECMA_BIGINT_GET_DIGITS (result_p, 0);
  memset (data_p, 0, zero_size);
  memcpy (data_p + zero_size, digits, digits_size);

  if (number < 0)
  {
    result_p->u.bigint_sign_and_size |= ECMA_BIGINT_SIGN;
  }

  return ecma_make_extended_primitive_value (result_p, ECMA_TYPE_BIGINT);
} /* ecma_bigint_number_to_bigint */

/**
 * Convert a value to BigInt value
 *
 * See also:
 *          ECMA-262 v11, 7.1.13
 *
 * @return ecma BigInt value or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
ecma_value_t
ecma_bigint_to_bigint (ecma_value_t value) /**< any value */
{
  if (ecma_is_value_boolean (value))
  {
    if (ecma_is_value_false (value))
    {
      return ECMA_BIGINT_ZERO;
    }

    ecma_extended_primitive_t *result_p = ecma_bigint_create (sizeof (ecma_bigint_digit_t));

    if (JERRY_UNLIKELY (result_p == NULL))
    {
      return ecma_bigint_raise_memory_error ();
    }

    *ECMA_BIGINT_GET_DIGITS (result_p, 0) = 1;
    return ecma_make_extended_primitive_value (result_p, ECMA_TYPE_BIGINT);
  }

  if (!ecma_is_value_string (value))
  {
    return ecma_raise_type_error (ECMA_ERR_MSG ("Value cannot be converted to BigInt"));
  }

  return ecma_bigint_parse_string_value (value, ECMA_BIGINT_PARSE_NO_OPTIONS);
} /* ecma_bigint_to_bigint */

/**
 * Compare two BigInt values
 *
 * @return true if they are the same, false otherwise
 */
bool
ecma_bigint_is_equal_to_bigint (ecma_value_t left_value, /**< left BigInt value */
                                ecma_value_t right_value) /**< right BigInt value */
{
  JERRY_ASSERT (ecma_is_value_bigint (left_value) && ecma_is_value_bigint (right_value));

  if (left_value == ECMA_BIGINT_ZERO)
  {
    return right_value == ECMA_BIGINT_ZERO;
  }
  else if (right_value == ECMA_BIGINT_ZERO)
  {
    return false;
  }

  ecma_extended_primitive_t *left_p = ecma_get_extended_primitive_from_value (left_value);
  ecma_extended_primitive_t *right_p = ecma_get_extended_primitive_from_value (right_value);

  if (left_p->u.bigint_sign_and_size != right_p->u.bigint_sign_and_size)
  {
    return false;
  }

  uint32_t size = ECMA_BIGINT_GET_SIZE (left_p);
  return memcmp (ECMA_BIGINT_GET_DIGITS (left_p, 0), ECMA_BIGINT_GET_DIGITS (right_p, 0), size) == 0;
} /* ecma_bigint_is_equal_to_bigint */

/**
 * Compare a BigInt value and a number
 *
 * @return true if they are the same, false otherwise
 */
bool
ecma_bigint_is_equal_to_number (ecma_value_t left_value, /**< left BigInt value */
                                ecma_number_t right_value) /**< right number value */
{
  JERRY_ASSERT (ecma_is_value_bigint (left_value));

  if (!ecma_number_is_finite (right_value))
  {
    return false;
  }

  if (left_value == ECMA_BIGINT_ZERO)
  {
    return right_value == 0;
  }

  ecma_extended_primitive_t *left_value_p = ecma_get_extended_primitive_from_value (left_value);

  /* Sign must be the same. */
  if (left_value_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN)
  {
    if (right_value > 0)
    {
      return false;
    }
  }
  else if (right_value < 0)
  {
    return false;
  }

  ecma_bigint_digit_t digits[3];
  uint32_t result = ecma_bigint_number_to_digits (right_value, digits);

  JERRY_ASSERT (ECMA_BIGINT_NUMBER_TO_DIGITS_GET_DIGITS (result) == 0
                || digits[ECMA_BIGINT_NUMBER_TO_DIGITS_GET_DIGITS (result) - 1] > 0);

  if (result & ECMA_BIGINT_NUMBER_TO_DIGITS_HAS_FRACTION)
  {
    return false;
  }

  uint32_t digits_size = ECMA_BIGINT_NUMBER_TO_DIGITS_GET_DIGITS_SIZE (result);
  uint32_t zero_size = ECMA_BIGINT_NUMBER_TO_DIGITS_GET_ZERO_SIZE (result);

  if (ECMA_BIGINT_GET_SIZE (left_value_p) != digits_size + zero_size)
  {
    return false;
  }

  ecma_bigint_digit_t *left_p = ECMA_BIGINT_GET_DIGITS (left_value_p, 0);
  ecma_bigint_digit_t *left_end_p = (ecma_bigint_digit_t *) (((uint8_t *) left_p) + zero_size);

  /* Check value bits first. */
  if (memcmp (left_end_p, digits, digits_size) != 0)
  {
    return false;
  }

  while (left_p < left_end_p)
  {
    if (*left_p++ != 0)
    {
      return false;
    }
  }

  return true;
} /* ecma_bigint_is_equal_to_number */

/**
 * Convert 0 to 1, and 1 to -1. Useful for getting sign.
 */
#define ECMA_BIGINT_TO_SIGN(value) (1 - (((int) (value)) << 1))

/**
 * Compare two BigInt values
 *
 * return -1, if left value < right value, 0 if they are equal, 1 otherwise
 */
int
ecma_bigint_compare_to_bigint (ecma_value_t left_value, /**< left BigInt value */
                               ecma_value_t right_value) /**< right BigInt value */
{
  JERRY_ASSERT (ecma_is_value_bigint (left_value) && ecma_is_value_bigint (right_value));

  ecma_extended_primitive_t *left_p = ecma_get_extended_primitive_from_value (left_value);
  ecma_extended_primitive_t *right_p = ecma_get_extended_primitive_from_value (right_value);

  uint32_t left_sign = left_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN;
  uint32_t right_sign = right_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN;

  if ((left_sign ^ right_sign) != 0)
  {
    return ECMA_BIGINT_TO_SIGN (left_sign);
  }

  return ecma_big_uint_compare (left_p, right_p);
} /* ecma_bigint_compare_to_bigint */

/**
 * Compare a BigInt value and a number
 *
 * return -1, if left value < right value, 0 if they are equal, 1 otherwise
 */
int
ecma_bigint_compare_to_number (ecma_value_t left_value, /**< left BigInt value */
                               ecma_number_t right_value) /**< right number value */
{
  JERRY_ASSERT (ecma_is_value_bigint (left_value));
  JERRY_ASSERT (!ecma_number_is_nan (right_value));

  int right_invert_sign = ECMA_BIGINT_TO_SIGN (right_value > 0);

  if (left_value == ECMA_BIGINT_ZERO)
  {
    if (right_value == 0)
    {
      return 0;
    }

    return right_invert_sign;
  }

  ecma_extended_primitive_t *left_value_p = ecma_get_extended_primitive_from_value (left_value);
  int left_sign = ECMA_BIGINT_TO_SIGN (left_value_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN);

  if (right_value == 0 || left_sign == right_invert_sign)
  {
    /* Second condition: a positive BigInt is always greater than any negative number, and the opposite is true. */
    return left_sign;
  }

  if (ecma_number_is_infinity (right_value))
  {
    /* Infinity is always bigger than any BigInt number. */
    return right_invert_sign;
  }

  ecma_bigint_digit_t digits[3];
  uint32_t result = ecma_bigint_number_to_digits (right_value, digits);

  JERRY_ASSERT (ECMA_BIGINT_NUMBER_TO_DIGITS_GET_DIGITS (result) == 0
                || digits[ECMA_BIGINT_NUMBER_TO_DIGITS_GET_DIGITS (result) - 1] > 0);

  uint32_t digits_size = ECMA_BIGINT_NUMBER_TO_DIGITS_GET_DIGITS_SIZE (result);

  if (digits_size == 0)
  {
    JERRY_ASSERT (result & ECMA_BIGINT_NUMBER_TO_DIGITS_HAS_FRACTION);
    /* The number is between [-1 .. 1] exclusive. */
    return left_sign;
  }

  uint32_t left_size = ECMA_BIGINT_GET_SIZE (left_value_p);
  uint32_t right_size = digits_size + ECMA_BIGINT_NUMBER_TO_DIGITS_GET_ZERO_SIZE (result);

  if (left_size != right_size)
  {
    return left_size > right_size ? left_sign : -left_sign;
  }

  ecma_bigint_digit_t *left_p = ECMA_BIGINT_GET_DIGITS (left_value_p, right_size);
  ecma_bigint_digit_t *left_end_p = (ecma_bigint_digit_t *) (((uint8_t *) left_p) - digits_size);
  ecma_bigint_digit_t *digits_p = (ecma_bigint_digit_t *) (((uint8_t *) digits) + digits_size);

  do
  {
    ecma_bigint_digit_t left = *(--left_p);
    ecma_bigint_digit_t right = *(--digits_p);

    if (left != right)
    {
      return left > right ? left_sign : -left_sign;
    }
  }
  while (left_p > left_end_p);

  left_end_p = ECMA_BIGINT_GET_DIGITS (left_value_p, 0);

  while (left_p > left_end_p)
  {
    if (*(--left_p) != 0)
    {
      return left_sign;
    }
  }

  return (result & ECMA_BIGINT_NUMBER_TO_DIGITS_HAS_FRACTION) ? -left_sign : 0;
} /* ecma_bigint_compare_to_number */

#undef ECMA_BIGINT_TO_SIGN

/**
 * Negate a non-zero BigInt value
 *
 * @return ecma BigInt value or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
ecma_value_t
ecma_bigint_negate (ecma_extended_primitive_t *value_p) /**< BigInt value */
{
  uint32_t size = ECMA_BIGINT_GET_SIZE (value_p);

  JERRY_ASSERT (size > 0 && ECMA_BIGINT_GET_LAST_DIGIT (value_p, size) != 0);

  ecma_extended_primitive_t *result_p = ecma_bigint_create (size);

  if (JERRY_UNLIKELY (result_p == NULL))
  {
    return ecma_bigint_raise_memory_error ();
  }

  memcpy (result_p + 1, value_p + 1, size);
  result_p->refs_and_type = ECMA_EXTENDED_PRIMITIVE_REF_ONE | ECMA_TYPE_BIGINT;
  result_p->u.bigint_sign_and_size = value_p->u.bigint_sign_and_size ^ ECMA_BIGINT_SIGN;

  return ecma_make_extended_primitive_value (result_p, ECMA_TYPE_BIGINT);
} /* ecma_bigint_negate */

/**
 * Add/subtract right BigInt value to/from left BigInt value
 *
 * @return ecma BigInt value or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
ecma_value_t
ecma_bigint_add_sub (ecma_value_t left_value, /**< left BigInt value */
                     ecma_value_t right_value, /**< right BigInt value */
                     bool is_add) /**< true if add operation should be performed */
{
  JERRY_ASSERT (ecma_is_value_bigint (left_value) && ecma_is_value_bigint (right_value));

  if (right_value == ECMA_BIGINT_ZERO)
  {
    return ecma_copy_value (left_value);
  }

  ecma_extended_primitive_t *right_p = ecma_get_extended_primitive_from_value (right_value);

  if (left_value == ECMA_BIGINT_ZERO)
  {
    if (!is_add)
    {
      return ecma_bigint_negate (right_p);
    }

    ecma_ref_extended_primitive (right_p);
    return right_value;
  }

  ecma_extended_primitive_t *left_p = ecma_get_extended_primitive_from_value (left_value);
  uint32_t sign = is_add ? 0 : ECMA_BIGINT_SIGN;

  if (((left_p->u.bigint_sign_and_size ^ right_p->u.bigint_sign_and_size) & ECMA_BIGINT_SIGN) == sign)
  {
    ecma_extended_primitive_t *result_p = ecma_big_uint_add (left_p, right_p);

    if (JERRY_UNLIKELY (result_p == NULL))
    {
      return ecma_bigint_raise_memory_error ();
    }

    result_p->u.bigint_sign_and_size |= left_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN;
    return ecma_make_extended_primitive_value (result_p, ECMA_TYPE_BIGINT);
  }

  int compare_result = ecma_big_uint_compare (left_p, right_p);
  ecma_extended_primitive_t *result_p;

  if (compare_result == 0)
  {
    return ECMA_BIGINT_ZERO;
  }

  if (compare_result > 0)
  {
    sign = left_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN;
    result_p = ecma_big_uint_sub (left_p, right_p);
  }
  else
  {
    sign = right_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN;

    if (!is_add)
    {
      sign ^= ECMA_BIGINT_SIGN;
    }

    result_p = ecma_big_uint_sub (right_p, left_p);
  }

  if (JERRY_UNLIKELY (result_p == NULL))
  {
    return ecma_bigint_raise_memory_error ();
  }

  result_p->u.bigint_sign_and_size |= sign;
  return ecma_make_extended_primitive_value (result_p, ECMA_TYPE_BIGINT);
} /* ecma_bigint_add_sub */

/**
 * Multiply two BigInt values
 *
 * @return ecma BigInt value or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
ecma_value_t
ecma_bigint_mul (ecma_value_t left_value, /**< left BigInt value */
                 ecma_value_t right_value) /**< right BigInt value */
{
  JERRY_ASSERT (ecma_is_value_bigint (left_value) && ecma_is_value_bigint (right_value));

  if (left_value == ECMA_BIGINT_ZERO || right_value == ECMA_BIGINT_ZERO)
  {
    return ECMA_BIGINT_ZERO;
  }

  ecma_extended_primitive_t *left_p = ecma_get_extended_primitive_from_value (left_value);
  ecma_extended_primitive_t *right_p = ecma_get_extended_primitive_from_value (right_value);
  uint32_t left_size = ECMA_BIGINT_GET_SIZE (left_p);
  uint32_t right_size = ECMA_BIGINT_GET_SIZE (right_p);

  if (left_size == sizeof (ecma_bigint_digit_t)
      && ECMA_BIGINT_GET_LAST_DIGIT (left_p, sizeof (ecma_bigint_digit_t)) == 1)
  {
    if (left_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN)
    {
      return ecma_bigint_negate (right_p);
    }

    ecma_ref_extended_primitive (right_p);
    return right_value;
  }

  if (right_size == sizeof (ecma_bigint_digit_t)
      && ECMA_BIGINT_GET_LAST_DIGIT (right_p, sizeof (ecma_bigint_digit_t)) == 1)
  {
    if (right_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN)
    {
      return ecma_bigint_negate (left_p);
    }

    ecma_ref_extended_primitive (left_p);
    return left_value;
  }

  ecma_extended_primitive_t *result_p = ecma_big_uint_mul (left_p, right_p);

  if (JERRY_UNLIKELY (result_p == NULL))
  {
    return ecma_bigint_raise_memory_error ();
  }

  uint32_t sign = (left_p->u.bigint_sign_and_size ^ right_p->u.bigint_sign_and_size) & ECMA_BIGINT_SIGN;
  result_p->u.bigint_sign_and_size |= sign;
  return ecma_make_extended_primitive_value (result_p, ECMA_TYPE_BIGINT);
} /* ecma_bigint_mul */

/**
 * Divide two BigInt values
 *
 * @return ecma BigInt value or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
ecma_value_t
ecma_bigint_div_mod (ecma_value_t left_value, /**< left BigInt value */
                     ecma_value_t right_value, /**< right BigInt value */
                     bool is_mod) /**< true if return with remainder */
{
  JERRY_ASSERT (ecma_is_value_bigint (left_value) && ecma_is_value_bigint (right_value));

  if (right_value == ECMA_BIGINT_ZERO)
  {
    return ecma_raise_range_error (ECMA_ERR_MSG ("BigInt division by zero"));
  }

  if (left_value == ECMA_BIGINT_ZERO)
  {
    return left_value;
  }

  ecma_extended_primitive_t *left_p = ecma_get_extended_primitive_from_value (left_value);
  ecma_extended_primitive_t *right_p = ecma_get_extended_primitive_from_value (right_value);

  int compare_result = ecma_big_uint_compare (left_p, right_p);
  ecma_extended_primitive_t *result_p;

  if (compare_result < 0)
  {
    if (!is_mod)
    {
      return ECMA_BIGINT_ZERO;
    }

    ecma_ref_extended_primitive (left_p);
    return left_value;
  }
  else if (compare_result == 0)
  {
    if (is_mod)
    {
      return ECMA_BIGINT_ZERO;
    }

    result_p = ecma_bigint_create (sizeof (ecma_bigint_digit_t));

    if (result_p != NULL)
    {
      *ECMA_BIGINT_GET_DIGITS (result_p, 0) = 1;
    }
  }
  else
  {
    result_p = ecma_big_uint_div_mod (left_p, right_p, is_mod);

    if (result_p == ECMA_BIGINT_POINTER_TO_ZERO)
    {
      return ECMA_BIGINT_ZERO;
    }
  }

  if (JERRY_UNLIKELY (result_p == NULL))
  {
    return ecma_bigint_raise_memory_error ();
  }

  if (is_mod)
  {
    result_p->u.bigint_sign_and_size |= left_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN;
  }
  else
  {
    uint32_t sign = (left_p->u.bigint_sign_and_size ^ right_p->u.bigint_sign_and_size) & ECMA_BIGINT_SIGN;
    result_p->u.bigint_sign_and_size |= sign;
  }

  return ecma_make_extended_primitive_value (result_p, ECMA_TYPE_BIGINT);
} /* ecma_bigint_div_mod */

/**
 * Shift left BigInt value to left or right
 *
 * @return ecma BigInt value or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
ecma_value_t
ecma_bigint_shift (ecma_value_t left_value, /**< left BigInt value */
                   ecma_value_t right_value, /**< right BigInt value */
                   bool is_left) /**< true if left shift operation should be performed */
{
  JERRY_ASSERT (ecma_is_value_bigint (left_value) && ecma_is_value_bigint (right_value));

  if (left_value == ECMA_BIGINT_ZERO)
  {
    return ECMA_BIGINT_ZERO;
  }

  ecma_extended_primitive_t *left_p = ecma_get_extended_primitive_from_value (left_value);

  if (right_value == ECMA_BIGINT_ZERO)
  {
    ecma_ref_extended_primitive (left_p);
    return left_value;
  }

  ecma_extended_primitive_t *right_p = ecma_get_extended_primitive_from_value (right_value);

  if (right_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN)
  {
    is_left = !is_left;
  }

  if (ECMA_BIGINT_GET_SIZE (right_p) > sizeof (ecma_bigint_digit_t))
  {
    if (is_left)
    {
      return ecma_bigint_raise_memory_error ();
    }

    return ECMA_BIGINT_ZERO;
  }

  ecma_extended_primitive_t *result_p;
  ecma_bigint_digit_t shift = ECMA_BIGINT_GET_LAST_DIGIT (right_p, sizeof (ecma_bigint_digit_t));

  if (is_left)
  {
    result_p = ecma_big_uint_shift_left (left_p, shift);
  }
  else
  {
    result_p = ecma_big_uint_shift_right (left_p, shift);

    if (result_p == ECMA_BIGINT_POINTER_TO_ZERO)
    {
      return ECMA_BIGINT_ZERO;
    }
  }

  if (JERRY_UNLIKELY (result_p == NULL))
  {
    return ecma_bigint_raise_memory_error ();
  }

  result_p->u.bigint_sign_and_size |= left_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN;
  return ecma_make_extended_primitive_value (result_p, ECMA_TYPE_BIGINT);
} /* ecma_bigint_shift */

/**
 * Convert the result to an ecma value
 *
 * @return ecma BigInt value or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
static ecma_value_t
ecma_bigint_bitwise_op (uint32_t operation_and_options, /**< bitwise operation type and options */
                        ecma_extended_primitive_t *left_value_p, /**< left BigInt value */
                        ecma_extended_primitive_t *right_value_p) /**< right BigInt value */
{
  ecma_extended_primitive_t *result_p;
  result_p = ecma_big_uint_bitwise_op (operation_and_options, left_value_p, right_value_p);

  if (JERRY_UNLIKELY (result_p == NULL))
  {
    return ecma_bigint_raise_memory_error ();
  }

  if (result_p == ECMA_BIGINT_POINTER_TO_ZERO)
  {
    return ECMA_BIGINT_ZERO;
  }

  if (operation_and_options & ECMA_BIG_UINT_BITWISE_INCREASE_RESULT)
  {
    result_p->u.bigint_sign_and_size |= ECMA_BIGINT_SIGN;
  }

  return ecma_make_extended_primitive_value (result_p, ECMA_TYPE_BIGINT);
} /* ecma_bigint_bitwise_op */

/**
 * Perform bitwise 'and' operations on two BigUInt numbers
 *
 * @return ecma BigInt value or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
ecma_value_t
ecma_bigint_and (ecma_value_t left_value, /**< left BigInt value */
                 ecma_value_t right_value) /**< right BigInt value */
{
  if (left_value == ECMA_BIGINT_ZERO || right_value == ECMA_BIGINT_ZERO)
  {
    return ECMA_BIGINT_ZERO;
  }

  ecma_extended_primitive_t *left_p = ecma_get_extended_primitive_from_value (left_value);
  ecma_extended_primitive_t *right_p = ecma_get_extended_primitive_from_value (right_value);

  if (!(left_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN))
  {
    if (!(right_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN))
    {
      return ecma_bigint_bitwise_op (ECMA_BIG_UINT_BITWISE_AND, left_p, right_p);
    }

    /* x & (-y) == x & ~(y-1) == x &~ (y-1) */
    uint32_t operation_and_options = ECMA_BIG_UINT_BITWISE_AND_NOT | ECMA_BIG_UINT_BITWISE_DECREASE_RIGHT;
    return ecma_bigint_bitwise_op (operation_and_options, left_p, right_p);
  }

  if (!(right_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN))
  {
    /* (-x) & y == ~(x-1) & y == y &~ (x-1) */
    uint32_t operation_and_options = ECMA_BIG_UINT_BITWISE_AND_NOT | ECMA_BIG_UINT_BITWISE_DECREASE_RIGHT;
    return ecma_bigint_bitwise_op (operation_and_options, right_p, left_p);
  }

  /* (-x) & (-y) == ~(x-1) & ~(y-1) == ~((x-1) | (y-1)) == -(((x-1) | (y-1)) + 1) */
  uint32_t operation_and_options = (ECMA_BIG_UINT_BITWISE_OR
                                    | ECMA_BIG_UINT_BITWISE_DECREASE_BOTH
                                    | ECMA_BIG_UINT_BITWISE_INCREASE_RESULT);
  return ecma_bigint_bitwise_op (operation_and_options, left_p, right_p);
} /* ecma_bigint_and */

/**
 * Perform bitwise 'or' operations on two BigUInt numbers
 *
 * @return ecma BigInt value or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
ecma_value_t
ecma_bigint_or (ecma_value_t left_value, /**< left BigInt value */
                ecma_value_t right_value) /**< right BigInt value */
{
  if (left_value == ECMA_BIGINT_ZERO)
  {
    return ecma_copy_value (right_value);
  }

  if (right_value == ECMA_BIGINT_ZERO)
  {
    return ecma_copy_value (left_value);
  }

  ecma_extended_primitive_t *left_p = ecma_get_extended_primitive_from_value (left_value);
  ecma_extended_primitive_t *right_p = ecma_get_extended_primitive_from_value (right_value);

  if (!(left_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN))
  {
    if (!(right_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN))
    {
      return ecma_bigint_bitwise_op (ECMA_BIG_UINT_BITWISE_OR, left_p, right_p);
    }

    /* x | (-y) == x | ~(y-1) == ~((y-1) &~ x) == -(((y-1) &~ x) + 1) */
    uint32_t operation_and_options = (ECMA_BIG_UINT_BITWISE_AND_NOT
                                      | ECMA_BIG_UINT_BITWISE_DECREASE_LEFT
                                      | ECMA_BIG_UINT_BITWISE_INCREASE_RESULT);
    return ecma_bigint_bitwise_op (operation_and_options, right_p, left_p);
  }

  if (!(right_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN))
  {
    /* (-x) | y == ~(x-1) | y == ~((x-1) &~ y) == -(((x-1) &~ y) + 1) */
    uint32_t operation_and_options = (ECMA_BIG_UINT_BITWISE_AND_NOT
                                      | ECMA_BIG_UINT_BITWISE_DECREASE_LEFT
                                      | ECMA_BIG_UINT_BITWISE_INCREASE_RESULT);
    return ecma_bigint_bitwise_op (operation_and_options, left_p, right_p);
  }

  /* (-x) | (-y) == ~(x-1) | ~(y-1) == ~((x-1) & (y-1)) = -(((x-1) & (y-1)) + 1) */
  uint32_t operation_and_options = (ECMA_BIG_UINT_BITWISE_AND
                                    | ECMA_BIG_UINT_BITWISE_DECREASE_BOTH
                                    | ECMA_BIG_UINT_BITWISE_INCREASE_RESULT);
  return ecma_bigint_bitwise_op (operation_and_options, left_p, right_p);
} /* ecma_bigint_or */

/**
 * Perform bitwise 'xor' operations on two BigUInt numbers
 *
 * @return ecma BigInt value or ECMA_VALUE_ERROR
 *         Returned value must be freed with ecma_free_value.
 */
ecma_value_t
ecma_bigint_xor (ecma_value_t left_value, /**< left BigInt value */
                 ecma_value_t right_value) /**< right BigInt value */
{
  if (left_value == ECMA_BIGINT_ZERO)
  {
    return ecma_copy_value (right_value);
  }

  if (right_value == ECMA_BIGINT_ZERO)
  {
    return ecma_copy_value (left_value);
  }

  ecma_extended_primitive_t *left_p = ecma_get_extended_primitive_from_value (left_value);
  ecma_extended_primitive_t *right_p = ecma_get_extended_primitive_from_value (right_value);

  if (!(left_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN))
  {
    if (!(right_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN))
    {
      return ecma_bigint_bitwise_op (ECMA_BIG_UINT_BITWISE_XOR, left_p, right_p);
    }

    /* x ^ (-y) == x ^ ~(y-1) == ~(x ^ (y-1)) == -((x ^ (y-1)) + 1) */
    uint32_t operation_and_options = (ECMA_BIG_UINT_BITWISE_XOR
                                      | ECMA_BIG_UINT_BITWISE_DECREASE_RIGHT
                                      | ECMA_BIG_UINT_BITWISE_INCREASE_RESULT);
    return ecma_bigint_bitwise_op (operation_and_options, left_p, right_p);
  }

  if (!(right_p->u.bigint_sign_and_size & ECMA_BIGINT_SIGN))
  {
    /* (-x) | y == ~(x-1) ^ y == ~((x-1) ^ y) == -(((x-1) ^ y) + 1) */
    uint32_t operation_and_options = (ECMA_BIG_UINT_BITWISE_XOR
                                      | ECMA_BIG_UINT_BITWISE_DECREASE_LEFT
                                      | ECMA_BIG_UINT_BITWISE_INCREASE_RESULT);
    return ecma_bigint_bitwise_op (operation_and_options, left_p, right_p);
  }

  /* (-x) ^ (-y) == ~(x-1) ^ ~(y-1) == (x-1) ^ (y-1) */
  uint32_t operation_and_options = ECMA_BIG_UINT_BITWISE_XOR | ECMA_BIG_UINT_BITWISE_DECREASE_BOTH;
  return ecma_bigint_bitwise_op (operation_and_options, left_p, right_p);
} /* ecma_bigint_xor */

#endif /* ENABLED (JERRY_BUILTIN_BIGINT) */
