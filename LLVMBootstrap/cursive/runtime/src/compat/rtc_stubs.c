#include <stdint.h>

#ifdef _WIN32
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>

typedef union __declspec(align(16)) CursiveRtHalfABIValue {
  __m128 vector;
  uint16_t half[8];
} CursiveRtHalfABIValue;
#endif

/*
 * Floating-point code generation references this CRT sentinel symbol.
 * Provide it in the runtime so /NODEFAULTLIB links still succeed.
 */
int _fltused = 0;

static float cursive_rt_float_from_bits(uint32_t bits) {
  union {
    uint32_t bits;
    float value;
  } u;
  u.bits = bits;
  return u.value;
}

static uint32_t cursive_rt_float_to_bits(float value) {
  union {
    uint32_t bits;
    float value;
  } u;
  u.value = value;
  return u.bits;
}

static float cursive_rt_extend_half_bits(uint16_t value) {
  uint32_t sign = ((uint32_t)value & 0x8000u) << 16;
  uint32_t exponent = ((uint32_t)value >> 10) & 0x1fu;
  uint32_t fraction = (uint32_t)value & 0x03ffu;

  if (exponent == 0u) {
    if (fraction == 0u) {
      return cursive_rt_float_from_bits(sign);
    }

    int normalized_exponent = -14;
    while ((fraction & 0x0400u) == 0u) {
      fraction <<= 1;
      normalized_exponent -= 1;
    }
    fraction &= 0x03ffu;
    return cursive_rt_float_from_bits(
        sign |
        ((uint32_t)(normalized_exponent + 127) << 23) |
        (fraction << 13));
  }

  if (exponent == 0x1fu) {
    uint32_t payload = fraction << 13;
    if (payload != 0u) {
      payload |= 0x00400000u;
    }
    return cursive_rt_float_from_bits(sign | 0x7f800000u | payload);
  }

  return cursive_rt_float_from_bits(
      sign | ((exponent + 112u) << 23) | (fraction << 13));
}

static uint16_t cursive_rt_trunc_float_to_half_bits(float value) {
  uint32_t bits = cursive_rt_float_to_bits(value);
  uint16_t sign = (uint16_t)((bits >> 16) & 0x8000u);
  uint32_t exponent = (bits >> 23) & 0xffu;
  uint32_t fraction = bits & 0x007fffffu;

  if (exponent == 0xffu) {
    uint16_t payload = (uint16_t)(fraction >> 13);
    if (payload != 0u) {
      payload |= 0x0200u;
    }
    return (uint16_t)(sign | 0x7c00u | payload);
  }

  if (exponent == 0u) {
    return sign;
  }

  int half_exponent = (int)exponent - 127 + 15;
  if (half_exponent >= 31) {
    return (uint16_t)(sign | 0x7c00u);
  }

  if (half_exponent <= 0) {
    if (half_exponent < -10) {
      return sign;
    }

    uint32_t mantissa = fraction | 0x00800000u;
    int shift = 14 - half_exponent;
    uint32_t half_fraction = mantissa >> shift;
    uint32_t remainder = mantissa & ((1u << shift) - 1u);
    uint32_t halfway = 1u << (shift - 1);
    if (remainder > halfway ||
        (remainder == halfway && (half_fraction & 1u) != 0u)) {
      half_fraction += 1u;
    }
    if (half_fraction == 0x0400u) {
      return (uint16_t)(sign | 0x0400u);
    }
    return (uint16_t)(sign | half_fraction);
  }

  uint32_t half_fraction = fraction >> 13;
  uint32_t remainder = fraction & 0x00001fffu;
  if (remainder > 0x00001000u ||
      (remainder == 0x00001000u && (half_fraction & 1u) != 0u)) {
    half_fraction += 1u;
  }
  if (half_fraction == 0x0400u) {
    half_fraction = 0u;
    half_exponent += 1;
    if (half_exponent >= 31) {
      return (uint16_t)(sign | 0x7c00u);
    }
  }

  return (uint16_t)(sign | ((uint16_t)half_exponent << 10) |
                    (uint16_t)half_fraction);
}

#if defined(_M_X64) || defined(__x86_64__)
float __extendhfsf2(__m128 value) {
  CursiveRtHalfABIValue abi_value;
  abi_value.vector = value;
  return cursive_rt_extend_half_bits(abi_value.half[0]);
}

__m128 __truncsfhf2(float value) {
  CursiveRtHalfABIValue abi_value;
  uint16_t bits = cursive_rt_trunc_float_to_half_bits(value);
  for (int lane = 0; lane < 8; ++lane) {
    abi_value.half[lane] = 0u;
  }
  abi_value.half[0] = bits;
  return abi_value.vector;
}
#else
float __extendhfsf2(uint16_t value) {
  return cursive_rt_extend_half_bits(value);
}

uint16_t __truncsfhf2(float value) {
  return cursive_rt_trunc_float_to_half_bits(value);
}
#endif
#endif

int _RTC_InitBase(void) {
  return 0;
}

int _RTC_Shutdown(void) {
  return 0;
}

void _RTC_CheckStackVars(void* frame, void* desc) {
  (void)frame;
  (void)desc;
}

void _RTC_CheckStackVars2(void* frame, void* desc) {
  (void)frame;
  (void)desc;
}

/*
 * Debug runtime checks may emit a call to _RTC_UninitUse when MSVC instruments
 * reads from potentially uninitialized locals. Provide a no-op stub so the
 * CRT-free runtime archive remains self-contained under /NODEFAULTLIB.
 */
void __cdecl _RTC_UninitUse(const char* varname) {
  (void)varname;
}

/*
 * MSVC may emit SEH unwind metadata that references __C_specific_handler.
 * Provide a minimal CRT-free stub so linking succeeds under /NODEFAULTLIB.
 */
int __cdecl __C_specific_handler(void* exception_record,
                                 void* establisher_frame,
                                 void* context_record,
                                 void* dispatcher_context) {
  (void)exception_record;
  (void)establisher_frame;
  (void)context_record;
  (void)dispatcher_context;
  return 0;
}
