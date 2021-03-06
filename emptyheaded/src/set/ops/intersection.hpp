#ifndef _INTERSECTION_H_
#define _INTERSECTION_H_

#define GALLOP_SIZE 16
#define VEC_T __m128i
#define COMPILER_LIKELY(x)     __builtin_expect((x),1)
#define COMPILER_RARELY(x)     __builtin_expect((x),0)
/**
 * The following macros (VEC_OR, VEC_ADD_PTEST,VEC_CMP_EQUAL,VEC_SET_ALL_TO_INT,VEC_LOAD_OFFSET,
 * ASM_LEA_ADD_BYTES are only used in the v1 procedure below.
 */
#define VEC_OR(dest, other)                                             \
    __asm volatile("por %1, %0" : "+x" (dest) : "x" (other) )

// // decltype is C++ and typeof is C
#define VEC_ADD_PTEST(var, add, xmm)      {                             \
        decltype(var) _new = var + add;                                   \
        __asm volatile("ptest %2, %2\n\t"                           \
                           "cmovnz %1, %0\n\t"                          \
                           : /* writes */ "+r" (var)                    \
                           : /* reads */  "r" (_new), "x" (xmm)         \
                           : /* clobbers */ "cc");                      \
    }


#define VEC_CMP_EQUAL(dest, other)                                      \
    __asm volatile("pcmpeqd %1, %0" : "+x" (dest) : "x" (other))

#define VEC_SET_ALL_TO_INT(reg, int32)                                  \
    __asm volatile("movd %1, %0; pshufd $0, %0, %0"                 \
                       : "=x" (reg) : "g" (int32) )

#define VEC_LOAD_OFFSET(xmm, ptr, bytes)                    \
    __asm volatile("movdqu %c2(%1), %0" : "=x" (xmm) :  \
                   "r" (ptr), "i" (bytes))

#define ASM_LEA_ADD_BYTES(ptr, bytes)                            \
    __asm volatile("lea %c1(%0), %0\n\t" :                       \
                   /* reads/writes %0 */  "+r" (ptr) :           \
                   /* reads */ "i" (bytes));

namespace ops{
  /**
   * Fast scalar scheme designed by N. Kurz.
   */
  inline size_t scalar(const uint32_t *A, const size_t lenA,
                const uint32_t *B, const size_t lenB, uint32_t *out) {
      const uint32_t *const initout(out);
      if (lenA == 0 || lenB == 0)
          return 0;

      const uint32_t *endA = A + lenA;
      const uint32_t *endB = B + lenB;

      while (1) {
          while (*A < *B) {
  SKIP_FIRST_COMPARE:
              if (++A == endA)
                  return (out - initout);
          }
          while (*A > *B) {
              if (++B == endB)
                  return (out - initout);
          }
          if (*A == *B) {
              #if WRITE_VECTOR == 1
              *out++ = *A;
              #else
              out++;
              #endif
              if (++A == endA || ++B == endB)
                  return (out - initout);
          } else {
              goto SKIP_FIRST_COMPARE;
          }
      }

      return (out - initout); // NOTREACHED
  }
  inline size_t match_scalar(const uint32_t *A, const size_t lenA,
                      const uint32_t *B, const size_t lenB,
                      uint32_t *out) {

      const uint32_t *initout = out;
      if (lenA == 0 || lenB == 0) return 0;

      const uint32_t *endA = A + lenA;
      const uint32_t *endB = B + lenB;

      while (1) {
          while (*A < *B) {
  SKIP_FIRST_COMPARE:
              if (++A == endA) goto FINISH;
          }
          while (*A > *B) {
              if (++B == endB) goto FINISH;
          }
          if (*A == *B) {
              *out++ = *A;
              if (++A == endA || ++B == endB) goto FINISH;
          } else {
              goto SKIP_FIRST_COMPARE;
          }
      }

  FINISH:
      return (out - initout);
  }
  /**
   * Intersections scheme designed by N. Kurz that works very
   * well when intersecting an array with another where the density
   * differential is small (between 2 to 10).
   *
   * It assumes that lenRare <= lenFreq.
   *
   * Note that this is not symmetric: flipping the rare and freq pointers
   * as well as lenRare and lenFreq could lead to significant performance
   * differences.
   *
   * The matchOut pointer can safely be equal to the rare pointer.
   *
   * This function  use inline assembly.
   */
  inline Set<uinteger>* set_intersect_v1(Set<uinteger> *C_in, const Set<uinteger> *A_in, const Set<uinteger> *B_in){
      const uint32_t *rare = (uint32_t*)A_in->data;
      size_t lenRare = A_in->cardinality;
      const uint32_t *freq = (uint32_t*)B_in->data;
      size_t lenFreq = B_in->cardinality;
      uint32_t *matchOut = (uint32_t*)C_in->data;

      assert(lenRare <= lenFreq);
      const uint32_t *matchOrig = matchOut;
      if (lenFreq == 0 || lenRare == 0){
        const size_t density = 0.0;
        C_in->cardinality = 0;
        C_in->number_of_bytes = (0)*sizeof(uint32_t);
        C_in->density = density;
        C_in->type= type::UINTEGER;
        return C_in;
      }

      const uint64_t kFreqSpace = 2 * 4 * (0 + 1) - 1;
      const uint64_t kRareSpace = 0;

      const uint32_t *stopFreq = &freq[lenFreq] - kFreqSpace;
      const uint32_t *stopRare = &rare[lenRare] - kRareSpace;

      VEC_T Rare;

      VEC_T F0, F1;

      if (COMPILER_RARELY((rare >= stopRare) || (freq >= stopFreq))) goto FINISH_SCALAR;

      uint64_t valRare;
      valRare = rare[0];
      VEC_SET_ALL_TO_INT(Rare, valRare);

      uint64_t maxFreq;
      maxFreq = freq[2 * 4 - 1];
      VEC_LOAD_OFFSET(F0, freq, 0 * sizeof(VEC_T)) ;
      VEC_LOAD_OFFSET(F1, freq, 1 * sizeof(VEC_T));

      if (COMPILER_RARELY(maxFreq < valRare)) goto ADVANCE_FREQ;

  ADVANCE_RARE:
      do {
          *matchOut = static_cast<uint32_t>(valRare);
          valRare = rare[1]; // for next iteration
          ASM_LEA_ADD_BYTES(rare, sizeof(*rare)); // rare += 1;

          if (COMPILER_RARELY(rare >= stopRare)) {
              rare -= 1;
              goto FINISH_SCALAR;
          }

          VEC_CMP_EQUAL(F0, Rare) ;
          VEC_CMP_EQUAL(F1, Rare);

          VEC_SET_ALL_TO_INT(Rare, valRare);

          VEC_OR(F0, F1);
  #ifdef __SSE4_1__
          VEC_ADD_PTEST(matchOut, 1, F0);
  #else
          matchOut += static_cast<uint32_t>(_mm_movemask_epi8(F0) != 0);
  #endif

          VEC_LOAD_OFFSET(F0, freq, 0 * sizeof(VEC_T)) ;
          VEC_LOAD_OFFSET(F1, freq, 1 * sizeof(VEC_T));

      } while (maxFreq >= valRare);

      uint64_t maxProbe;

  ADVANCE_FREQ:
      do {
          const uint64_t kProbe = (0 + 1) * 2 * 4;
          const uint32_t *probeFreq = freq + kProbe;
          maxProbe = freq[(0 + 2) * 2 * 4 - 1];

          if (COMPILER_RARELY(probeFreq >= stopFreq)) {
              goto FINISH_SCALAR;
          }

          freq = probeFreq;

      } while (maxProbe < valRare);

      maxFreq = maxProbe;

      VEC_LOAD_OFFSET(F0, freq, 0 * sizeof(VEC_T)) ;
      VEC_LOAD_OFFSET(F1, freq, 1 * sizeof(VEC_T));

      goto ADVANCE_RARE;

      size_t count;
  FINISH_SCALAR:
      count = matchOut - matchOrig;

      lenFreq = stopFreq + kFreqSpace - freq;
      lenRare = stopRare + kRareSpace - rare;

      size_t tail = match_scalar(freq, lenFreq, rare, lenRare, matchOut);

    const size_t density = 0.0;
    C_in->cardinality = count + tail;
    C_in->number_of_bytes = (count+tail)*sizeof(uint32_t);
    C_in->density = density;
    C_in->type= type::UINTEGER;

    return C_in;  
  }


  /**
   * This intersection function is similar to v1, but is faster when
   * the difference between lenRare and lenFreq is large, but not too large.
   * It assumes that lenRare <= lenFreq.
   *
   * Note that this is not symmetric: flipping the rare and freq pointers
   * as well as lenRare and lenFreq could lead to significant performance
   * differences.
   *
   * The matchOut pointer can safely be equal to the rare pointer.
   *
   * This function DOES NOT use inline assembly instructions. Just intrinsics.
   */
  inline Set<uinteger>* set_intersect_v3(Set<uinteger> *C_in, const Set<uinteger> *A_in, const Set<uinteger> *B_in){
      const uint32_t *rare = (uint32_t*)A_in->data;
      const size_t lenRare = A_in->cardinality;
      const uint32_t *freq = (uint32_t*)B_in->data;
      const size_t lenFreq = B_in->cardinality;
      uint32_t *out = (uint32_t*)C_in->data;

      if (lenFreq == 0 || lenRare == 0){
        const size_t density = 0.0;
        C_in->cardinality = 0;
        C_in->number_of_bytes = (0)*sizeof(uint32_t);
        C_in->density = density;
        C_in->type= type::UINTEGER;
        return C_in;
      }
      assert(lenRare <= lenFreq);
      const uint32_t *const initout(out);
      typedef __m128i vec;
      const uint32_t veclen = sizeof(vec) / sizeof(uint32_t);
      const size_t vecmax = veclen - 1;
      const size_t freqspace = 32 * veclen;
      const size_t rarespace = 1;

      const uint32_t *stopFreq = freq + lenFreq - freqspace;
      const uint32_t *stopRare = rare + lenRare - rarespace;
      if (freq > stopFreq) {
        const size_t final_count = scalar(freq, lenFreq, rare, lenRare, out);
        const size_t density = 0.0;
        C_in->cardinality = final_count;
        C_in->number_of_bytes = (final_count)*sizeof(uint32_t);
        C_in->density = density;
        C_in->type= type::UINTEGER;
        return C_in;
      }
      while (freq[veclen * 31 + vecmax] < *rare) {
          freq += veclen * 32;
          if (freq > stopFreq)
              goto FINISH_SCALAR;
      }
      for (; rare < stopRare; ++rare) {
          const uint32_t matchRare = *rare;//nextRare;
          const vec Match = _mm_set1_epi32(matchRare);
          while (freq[veclen * 31 + vecmax] < matchRare) { // if no match possible
              freq += veclen * 32; // advance 32 vectors
              if (freq > stopFreq)
                  goto FINISH_SCALAR;
          }
          vec Q0, Q1, Q2, Q3;
          if (freq[veclen * 15 + vecmax] >= matchRare) {
              if (freq[veclen * 7 + vecmax] < matchRare) {
                  Q0 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 8), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 9), Match));
                  Q1 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 10), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 11), Match));

                  Q2 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 12), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 13), Match));
                  Q3 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 14), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 15), Match));
              } else {
                  Q0 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 4), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 5), Match));
                  Q1 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 6), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 7), Match));
                  Q2 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 0), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 1), Match));
                  Q3 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 2), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 3), Match));
              }
          } else {
              if (freq[veclen * 23 + vecmax] < matchRare) {
                  Q0 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 8 + 16), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 9 + 16), Match));
                  Q1 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 10 + 16), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 11 + 16), Match));

                  Q2 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 12 + 16), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 13 + 16), Match));
                  Q3 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 14 + 16), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 15 + 16), Match));
              } else {
                  Q0 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 4 + 16), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 5 + 16), Match));
                  Q1 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 6 + 16), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 7 + 16), Match));
                  Q2 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 0 + 16), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 1 + 16), Match));
                  Q3 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 2 + 16), Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 3 + 16), Match));
              }

          }
          const vec F0 = _mm_or_si128(_mm_or_si128(Q0, Q1), _mm_or_si128(Q2, Q3));
  #ifdef __SSE4_1__
          if (_mm_testz_si128(F0, F0)) {
  #else 
          if (!_mm_movemask_epi8(F0)) {
  #endif 
          } else {
              *out++ = matchRare;
          }
      }

  FINISH_SCALAR: 
    const size_t final_count = (out - initout) + scalar(freq,stopFreq + freqspace - freq, rare, stopRare + rarespace - rare, out);
    const size_t density = 0.0;
    C_in->cardinality = final_count;
    C_in->number_of_bytes = (final_count)*sizeof(uint32_t);
    C_in->density = density;
    C_in->type= type::UINTEGER;
    return C_in;
  }


  /**
   * This is the SIMD galloping function. This intersection function works well
   * when lenRare and lenFreq have vastly different values.
   *
   * It assumes that lenRare <= lenFreq.
   *
   * Note that this is not symmetric: flipping the rare and freq pointers
   * as well as lenRare and lenFreq could lead to significant performance
   * differences.
   *
   * The matchOut pointer can safely be equal to the rare pointer.
   *
   * This function DOES NOT use assembly. It only relies on intrinsics.
   */

  inline Set<uinteger>* set_intersect_galloping(Set<uinteger> *C_in, const Set<uinteger> *A_in, const Set<uinteger> *B_in){
      const uint32_t *rare = (uint32_t*)A_in->data;
      const size_t lenRare = A_in->cardinality;
      const uint32_t *freq = (uint32_t*)B_in->data;
      const size_t lenFreq = B_in->cardinality;
      uint32_t *out = (uint32_t*)C_in->data;

      if (lenFreq == 0 || lenRare == 0){
        const size_t density = 0.0;
        C_in->cardinality = 0;
        C_in->number_of_bytes = (0)*sizeof(uint32_t);
        C_in->density = density;
        C_in->type= type::UINTEGER;
        return C_in;
      }
      assert(lenRare <= lenFreq);
      const uint32_t *const initout(out);
      typedef __m128i vec;
      const uint32_t veclen = sizeof(vec) / sizeof(uint32_t);
      const size_t vecmax = veclen - 1;
      const size_t freqspace = 32 * veclen;
      const size_t rarespace = 1;

      const uint32_t *stopFreq = freq + lenFreq - freqspace;
      const uint32_t *stopRare = rare + lenRare - rarespace;
      if (freq > stopFreq) {
        const size_t final_count = scalar(freq, lenFreq, rare, lenRare, out);
        const size_t density = 0.0;
        C_in->cardinality = final_count;
        C_in->number_of_bytes = (final_count)*sizeof(uint32_t);
        C_in->density = density;
        C_in->type= type::UINTEGER;
        return C_in;
      }
      for (; rare < stopRare; ++rare) {
          const uint32_t matchRare = *rare;//nextRare;
          const vec Match = _mm_set1_epi32(matchRare);

          if (freq[veclen * 31 + vecmax] < matchRare) { // if no match possible
              uint32_t offset = 1;
              if (freq + veclen  * 32 > stopFreq) {
                  freq += veclen * 32;
                  goto FINISH_SCALAR;
              }
              while (freq[veclen * offset * 32 + veclen * 31 + vecmax]
                     < matchRare) { // if no match possible
                  if (freq + veclen * (2 * offset) * 32 <= stopFreq) {
                      offset *= 2;
                  } else if (freq + veclen * (offset + 1) * 32 <= stopFreq) {
                      offset = static_cast<uint32_t>((stopFreq - freq) / (veclen * 32));
                      //offset += 1;
                      if (freq[veclen * offset * 32 + veclen * 31 + vecmax]
                          < matchRare) {
                          freq += veclen * offset * 32;
                          goto FINISH_SCALAR;
                      } else {
                          break;
                      }
                  } else {
                      freq += veclen * offset * 32;
                      goto FINISH_SCALAR;
                  }
              }
              uint32_t lower = offset / 2;
              while (lower + 1 != offset) {
                  const uint32_t mid = (lower + offset) / 2;
                  if (freq[veclen * mid * 32 + veclen * 31 + vecmax]
                      < matchRare)
                      lower = mid;
                  else
                      offset = mid;
              }
              freq += veclen * offset * 32;
          }
          vec Q0, Q1, Q2, Q3;
          if (freq[veclen * 15 + vecmax] >= matchRare) {
              if (freq[veclen * 7 + vecmax] < matchRare) {
                  Q0
                      = _mm_or_si128(
                            _mm_cmpeq_epi32(
                                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 8), Match),
                            _mm_cmpeq_epi32(
                                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 9), Match));
                  Q1 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 10),
                                           Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 11),
                                           Match));

                  Q2 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 12),
                                           Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 13),
                                           Match));
                  Q3 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 14),
                                           Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 15),
                                           Match));
              } else {
                  Q0
                      = _mm_or_si128(
                            _mm_cmpeq_epi32(
                                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 4), Match),
                            _mm_cmpeq_epi32(
                                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 5), Match));
                  Q1
                      = _mm_or_si128(
                            _mm_cmpeq_epi32(
                                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 6), Match),
                            _mm_cmpeq_epi32(
                                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 7), Match));
                  Q2
                      = _mm_or_si128(
                            _mm_cmpeq_epi32(
                                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 0), Match),
                            _mm_cmpeq_epi32(
                                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 1), Match));
                  Q3
                      = _mm_or_si128(
                            _mm_cmpeq_epi32(
                                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 2), Match),
                            _mm_cmpeq_epi32(
                                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 3), Match));
              }
          } else {
              if (freq[veclen * 23 + vecmax] < matchRare) {
                  Q0 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 8 + 16),
                                           Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 9 + 16),
                                           Match));
                  Q1 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 10 + 16),
                                           Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 11 + 16),
                                           Match));

                  Q2 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 12 + 16),
                                           Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 13 + 16),
                                           Match));
                  Q3 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 14 + 16),
                                           Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 15 + 16),
                                           Match));
              } else {
                  Q0 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 4 + 16),
                                           Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 5 + 16),
                                           Match));
                  Q1 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 6 + 16),
                                           Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 7 + 16),
                                           Match));
                  Q2 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 0 + 16),
                                           Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 1 + 16),
                                           Match));
                  Q3 = _mm_or_si128(
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 2 + 16),
                                           Match),
                           _mm_cmpeq_epi32(_mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 3 + 16),
                                           Match));
              }

          }
          const vec F0 = _mm_or_si128(_mm_or_si128(Q0, Q1), _mm_or_si128(Q2, Q3));
  #ifdef __SSE4_1__
          if (_mm_testz_si128(F0, F0)) {
  #else 
          if (!_mm_movemask_epi8(F0)) {
  #endif 
          } else {
              *out++ = matchRare;
          }
      }

  FINISH_SCALAR: 
    const size_t final_count = (out - initout) + scalar(freq,stopFreq + freqspace - freq, rare, stopRare + rarespace - rare, out);
    const size_t density = 0.0;
    C_in->cardinality = final_count;
    C_in->number_of_bytes = (final_count)*sizeof(uint32_t);
    C_in->density = density;
    C_in->type= type::UINTEGER;
    return C_in;
  }

  inline Set<uinteger>* set_intersect_ibm(Set<uinteger> *C_in, const Set<uinteger> *A_in, const Set<uinteger> *B_in){
    uint32_t * const C = (uint32_t*) C_in->data; 
    const uint32_t * const A = (uint32_t*) A_in->data;
    const uint32_t * const B = (uint32_t*) B_in->data;
    const size_t s_a = A_in->cardinality;
    const size_t s_b = B_in->cardinality;

    const size_t st_a = (s_a / SHORTS_PER_REG) * SHORTS_PER_REG;
    const size_t st_b = (s_b / SHORTS_PER_REG) * SHORTS_PER_REG;

    size_t count = 0;
    size_t i_a = 0, i_b = 0;

    while(i_a < st_a && i_b < st_b){
      //Pull in 4 uint 32's
      const __m128i v_a_1_32 = _mm_loadu_si128((__m128i*)&A[i_a]);
      const __m128i v_a_2_32 = _mm_loadu_si128((__m128i*)&A[i_a+(SHORTS_PER_REG/2)]);

      /*std::cout << std::endl;
      std::cout << "ORIGINAL DATA" << std::endl;
      type::_mm128i_print(v_a_1_32);
      type::_mm128i_print(v_a_2_32);*/


      //shuffle to std::get lower 16 bits only in one register
      const __m128i v_a_l1 = _mm_shuffle_epi8(v_a_1_32,_mm_set_epi8(uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x0D),uint8_t(0x0C),uint8_t(0x09),uint8_t(0x08),uint8_t(0x05),uint8_t(0x04),uint8_t(0x01),uint8_t(0x0)));
      const __m128i v_a_l2 = _mm_shuffle_epi8(v_a_2_32,_mm_set_epi8(uint8_t(0x0D),uint8_t(0x0C),uint8_t(0x09),uint8_t(0x08),uint8_t(0x05),uint8_t(0x04),uint8_t(0x01),uint8_t(0x0),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80)));
      const __m128i v_a_l = _mm_or_si128(v_a_l1,v_a_l2);

      const __m128i v_b_1_32 = _mm_loadu_si128((__m128i*)&B[i_b]);
      const __m128i v_b_2_32 = _mm_loadu_si128((__m128i*)&B[i_b+(SHORTS_PER_REG/2)]);

      //type::_mm128i_print(v_b_1_32);
      //type::_mm128i_print(v_b_2_32);

      const __m128i v_b_l1 = _mm_shuffle_epi8(v_b_1_32,_mm_set_epi8(uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x0D),uint8_t(0x0C),uint8_t(0x09),uint8_t(0x08),uint8_t(0x05),uint8_t(0x04),uint8_t(0x01),uint8_t(0x0)));
      const __m128i v_b_l2 = _mm_shuffle_epi8(v_b_2_32,_mm_set_epi8(uint8_t(0x0D),uint8_t(0x0C),uint8_t(0x09),uint8_t(0x08),uint8_t(0x05),uint8_t(0x04),uint8_t(0x01),uint8_t(0x0),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80)));
      const __m128i v_b_l = _mm_or_si128(v_b_l1,v_b_l2);
      
     // __m128i res_v = _mm_cmpistrm(v_b, v_a,
     //         _SIDD_UWORD_OPS|_SIDD_CMP_EQUAL_ANY|_SIDD_BIT_MASK);
      const __m128i res_vl = _mm_cmpestrm(v_b_l, SHORTS_PER_REG, v_a_l, SHORTS_PER_REG,
              _SIDD_UWORD_OPS|_SIDD_CMP_EQUAL_ANY|_SIDD_BIT_MASK);
      const uint32_t result_l = _mm_extract_epi32(res_vl, 0);

     // std::cout << "LOWER " << hex  << result_l << dec << std::endl;

      if(result_l != 0){
        //shuffle to std::get upper 16 bits only in one register
        const __m128i v_a_u1 = _mm_shuffle_epi8(v_a_1_32,_mm_set_epi8(uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x0F),uint8_t(0x0E),uint8_t(0x0B),uint8_t(0x0A),uint8_t(0x07),uint8_t(0x06),uint8_t(0x03),uint8_t(0x2)));
        const __m128i v_a_u2 = _mm_shuffle_epi8(v_a_2_32,_mm_set_epi8(uint8_t(0x0F),uint8_t(0x0E),uint8_t(0x0B),uint8_t(0x0A),uint8_t(0x07),uint8_t(0x06),uint8_t(0x03),uint8_t(0x2),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80)));
        const __m128i v_a_u = _mm_or_si128(v_a_u1,v_a_u2);

        const __m128i v_b_1_32 = _mm_loadu_si128((__m128i*)&B[i_b]);
        const __m128i v_b_2_32 = _mm_loadu_si128((__m128i*)&B[i_b+(SHORTS_PER_REG/2)]);

        const __m128i v_b_u1 = _mm_shuffle_epi8(v_b_1_32,_mm_set_epi8(uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x0F),uint8_t(0x0E),uint8_t(0x0B),uint8_t(0x0A),uint8_t(0x07),uint8_t(0x06),uint8_t(0x03),uint8_t(0x2)));
        const __m128i v_b_u2 = _mm_shuffle_epi8(v_b_2_32,_mm_set_epi8(uint8_t(0x0F),uint8_t(0x0E),uint8_t(0x0B),uint8_t(0x0A),uint8_t(0x07),uint8_t(0x06),uint8_t(0x03),uint8_t(0x2),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80),uint8_t(0x80)));
        const __m128i v_b_u = _mm_or_si128(v_b_u1,v_b_u2);

        const __m128i res_vu = _mm_cmpestrm(v_b_u, SHORTS_PER_REG, v_a_u, SHORTS_PER_REG,
                _SIDD_UWORD_OPS|_SIDD_CMP_EQUAL_ANY|_SIDD_BIT_MASK);
        const uint32_t result_u = _mm_extract_epi32(res_vu, 0);

        //std::cout << "UPPER " << hex  << result_l << dec << std::endl;

        const uint32_t w_bitmask = result_u & result_l;
        //std::cout << count << " BITMASK: " << hex  << w_bitmask << dec << std::endl;
        if(w_bitmask != 0){
          const size_t start_index = _mm_popcnt_u32((~w_bitmask)&(w_bitmask-1));
          const size_t A_pos = start_index+i_a;
          const size_t A_end = 8-start_index;
          const size_t B_pos = i_b;
          const size_t B_end = 8;
          count += scalar(&A[A_pos],A_end,&B[B_pos],B_end,&C[count]);
        }
      } 
      if(A[i_a+7] > B[i_b+7]){
        goto advanceB;
      } else if (A[i_a+7] < B[i_b+7]){
        goto advanceA;
      } else{
        goto advanceAB;
      }
      advanceA:
        i_a += 8;
        continue;
      advanceB:
        i_b += 8;
        continue;
      advanceAB:
        i_a += 8;
        i_b += 8;
        continue;
    }

    // intersect the tail using scalar intersection
    count += scalar(&A[i_a],s_a-i_a,&B[i_b],s_b-i_b,&C[count]);

    //XXX: Fix
    const double density = 0.0;//((count > 0) ? ((double)count/(C[count]-C[0])) : 0.0);

    C_in->cardinality = count;
    C_in->number_of_bytes = count*sizeof(uint32_t);
    C_in->density = density;
    C_in->type= type::UINTEGER;

    return C_in;
  }

  inline Set<uinteger>* set_intersect_standard(Set<uinteger> *C_in, const Set<uinteger> *A_in, const Set<uinteger> *B_in){
    uint32_t * const C = (uint32_t*) C_in->data; 
    const uint32_t * const A = (uint32_t*) A_in->data;
    const uint32_t * const B = (uint32_t*) B_in->data;
    const size_t s_a = A_in->cardinality;
    const size_t s_b = B_in->cardinality;

    size_t count = 0;
    size_t i_a = 0, i_b = 0;

    assert( ((size_t) A % 16) == 0 );
    assert( ((size_t) B % 16) == 0 );

    // trim lengths to be a multiple of 4
    #if VECTORIZE == 1
    size_t st_a = (s_a / 4) * 4;
    size_t st_b = (s_b / 4) * 4;

    while(i_a < st_a && i_b < st_b) {
      //[ load segments of four 32-bit elements
      __m128i v_a = _mm_load_si128((__m128i*)&A[i_a]);
      __m128i v_b = _mm_load_si128((__m128i*)&B[i_b]);
      //]

      //[ move pointers
      uint32_t a_max = A[i_a+3];
      uint32_t b_max = B[i_b+3];
      i_a += (a_max <= b_max) * 4;
      i_b += (a_max >= b_max) * 4;
      //]

      //[ compute mask of common elements
      const uint32_t right_cyclic_shift = _MM_SHUFFLE(0,3,2,1);
      __m128i cmp_mask1 = _mm_cmpeq_epi32(v_a, v_b);    // pairwise comparison
      v_b = _mm_shuffle_epi32(v_b, right_cyclic_shift);       // shuffling
      __m128i cmp_mask2 = _mm_cmpeq_epi32(v_a, v_b);    // again...
      v_b = _mm_shuffle_epi32(v_b, right_cyclic_shift);
      __m128i cmp_mask3 = _mm_cmpeq_epi32(v_a, v_b);    // and again...
      v_b = _mm_shuffle_epi32(v_b, right_cyclic_shift);
      __m128i cmp_mask4 = _mm_cmpeq_epi32(v_a, v_b);    // and again.
      __m128i cmp_mask = _mm_or_si128(
              _mm_or_si128(cmp_mask1, cmp_mask2),
              _mm_or_si128(cmp_mask3, cmp_mask4)
      ); // OR-ing of comparison masks
      // convert the 128-bit mask to the 4-bit mask
      uint32_t mask = _mm_movemask_ps((__m128)cmp_mask);
      //]

      //[ copy out common elements
      #if WRITE_VECTOR == 1
      //std::cout << "mask: " << mask << std::endl;
      __m128i p = _mm_shuffle_epi8(v_a, shuffle_mask32[mask]);
      _mm_storeu_si128((__m128i*)&C[count], p);
      //std::cout << "C[" << count << "]: " << C[count] << std::endl;

      #endif

      count += _mm_popcnt_u32(mask); // a number of elements is a weight of the mask
      //]
    }
    #endif

    // intersect the tail using scalar intersection
    count += scalar(&A[i_a],s_a-i_a,&B[i_b],s_b-i_b,&C[count]);

    #if WRITE_VECTOR == 0
    (void) C;
    #endif

    //XXX: Fix
    const double density = 0.0;//((count > 0) ? ((double)count/(C[count]-C[0])) : 0.0);
    
    C_in->cardinality = count;
    C_in->number_of_bytes = count*sizeof(uint32_t);
    C_in->density = density;
    C_in->type= type::UINTEGER;

    return C_in;  
  }
  inline Set<uinteger>* set_intersect(Set<uinteger> *C_in, const Set<uinteger> *A_in, const Set<uinteger> *B_in) {
    const Set<uinteger> *rare = (A_in->cardinality > B_in->cardinality) ? B_in:A_in;
    const Set<uinteger> *freq = (A_in->cardinality > B_in->cardinality) ? A_in:B_in;
    const unsigned long min_size = 1;

    //return set_intersect_standard(C_in, rare, freq);

    if(std::max(A_in->cardinality,B_in->cardinality) / std::max(min_size, std::min(A_in->cardinality,B_in->cardinality)) > 32)
      return set_intersect_galloping(C_in, rare, freq);
    else
      return set_intersect_standard(C_in, rare, freq);
  }

  inline Set<bitset>* set_intersect(Set<bitset> *C_in, const Set<bitset> *A_in, const Set<bitset> *B_in){
    long count = 0l;
    C_in->number_of_bytes = 0;

    if(A_in->number_of_bytes > 0 && B_in->number_of_bytes > 0){
      const uint64_t *a_index = (uint64_t*) A_in->data;
      const uint64_t *b_index = (uint64_t*) B_in->data;

      uint64_t * const C = (uint64_t*)(C_in->data+sizeof(uint64_t));
      const uint64_t * const A = (uint64_t*)(A_in->data+sizeof(uint64_t));
      const uint64_t * const B = (uint64_t*)(B_in->data+sizeof(uint64_t));
      const size_t s_a = ((A_in->number_of_bytes-sizeof(uint64_t))/sizeof(uint64_t));
      const size_t s_b = ((B_in->number_of_bytes-sizeof(uint64_t))/sizeof(uint64_t));

      #if WRITE_VECTOR == 0
      (void) C;
      #endif

      const bool a_big = a_index[0] > b_index[0];
      const uint64_t start_index = (a_big) ? a_index[0] : b_index[0];
      const uint64_t a_start_index = (a_big) ? 0:(b_index[0]-a_index[0]);
      const uint64_t b_start_index = (a_big) ? (a_index[0]-b_index[0]):0;

      const uint64_t end_index = ((a_index[0]+s_a) > (b_index[0]+s_b)) ? (b_index[0]+s_b):(a_index[0]+s_a);
      const uint64_t total_size = (start_index > end_index) ? 0:(end_index-start_index);

      //16 uint16_ts
      //8 ints
      //4 longs
      size_t i = 0;

      #if WRITE_VECTOR == 1
      uint64_t *c_index = (uint64_t*) C_in->data;
      c_index[0] = start_index;
      #endif

      #if VECTORIZE == 1
      uint64_t tmp[4];
      while((i+3) < total_size){
        const __m256 a1 = _mm256_loadu_ps((const float*)(A + i + a_start_index));
        const __m256 a2 = _mm256_loadu_ps((const float*)(B + i + b_start_index));
        const __m256 r = _mm256_and_ps(a2, a1);

        #if WRITE_VECTOR == 1
        _mm256_storeu_ps((float*)(C + i), r);
        #endif

        _mm256_storeu_ps((float*)tmp, r);
        count += _mm_popcnt_u64(tmp[0]);
        count += _mm_popcnt_u64(tmp[1]);
        count += _mm_popcnt_u64(tmp[2]);
        count += _mm_popcnt_u64(tmp[3]);

        i += 4;
      }
      #endif

      for(; i < total_size; i++){
        const uint64_t result = A[i+a_start_index] & B[i+b_start_index];

        #if WRITE_VECTOR == 1
        C[i] = result;
        #endif

        count += _mm_popcnt_u64(result);
      }
      C_in->number_of_bytes = total_size*sizeof(uint64_t)+sizeof(uint64_t);
    }
    const double density = 0.0;//(count > 0) ? (double)count/(8*small_length) : 0.0;

    C_in->cardinality = count;
    C_in->density = density;
    C_in->type= type::BITSET;

    return C_in;
  }
  inline Set<uinteger>* set_intersect(Set<uinteger> *C_in,const Set<uinteger> *A_in,const Set<bitset> *B_in){
    uint32_t * const C = (uint32_t*)C_in->data;
    const uint32_t * const A = (uint32_t*)A_in->data;
    const size_t s_a = A_in->cardinality;
    const size_t s_b = (B_in->number_of_bytes > 0) ? (B_in->number_of_bytes-sizeof(uint64_t))/sizeof(uint64_t):0;
    const uint64_t start_index = (B_in->number_of_bytes > 0) ? ((uint64_t*)B_in->data)[0]:0;

    const uint64_t * const B = (uint64_t*)(B_in->data+sizeof(uint64_t));

    #if WRITE_VECTOR == 0
    (void) C;
    #endif

    size_t count = 0;
    for(size_t i = 0; i < s_a; i++){
      const uint32_t cur = A[i];
      const size_t cur_index = bitset::word_index(cur);
      if((cur_index < (s_b+start_index)) && (cur_index >= start_index) && bitset::is_set(cur,B,start_index)){
        #if WRITE_VECTOR == 1
        C[count] = cur;
        #endif
        count++;
      } else if(cur_index >= (s_b+start_index)){
        break;
      }
    }
    // XXX: Correct density computation
    const double density = 0.0;//((count > 1) ? ((double)count/(C[count - 1]-C[0])) : 0.0);

    C_in->cardinality = count;
    C_in->number_of_bytes = count*sizeof(uint32_t);
    C_in->density = density;
    C_in->type= type::UINTEGER;

    return C_in;
  }
  inline Set<uinteger>* set_intersect(Set<uinteger> *C_in,const Set<bitset> *A_in,const Set<uinteger> *B_in){
    return set_intersect(C_in,B_in,A_in);
  }
  inline Set<hybrid>* set_intersect(Set<hybrid> *C_in,const Set<hybrid> *A_in,const Set<hybrid> *B_in){
    if(A_in->cardinality == 0 || B_in->cardinality == 0){
      C_in->cardinality = 0;
      C_in->number_of_bytes = 0;
      return C_in;
    }

    switch (A_in->type) {
        case type::UINTEGER:
          switch (B_in->type) {
            case type::UINTEGER:
              #ifdef STATS
              debug::num_uint_uint++;
              #endif
              return (Set<hybrid>*)set_intersect((Set<uinteger>*)C_in,(const Set<uinteger>*)A_in,(const Set<uinteger>*)B_in);
              break;
            case type::BITSET:
              #ifdef STATS
              type::num_uint_bs++;
              #endif
              return (Set<hybrid>*)set_intersect((Set<uinteger>*)C_in,(const Set<uinteger>*)A_in,(const Set<bitset>*)B_in);
              break;
            default:
              break;
          }
        break;
        case type::BITSET:
          switch (B_in->type) {
            case type::UINTEGER:
              #ifdef STATS
              debug::num_uint_bs++;
              #endif
              return (Set<hybrid>*)set_intersect((Set<uinteger>*)C_in,(const Set<uinteger>*)B_in,(const Set<bitset>*)A_in);
            break;
            case type::BITSET:
              #ifdef STATS
              debug::num_bs_bs++;
              #endif
              return (Set<hybrid>*)set_intersect((Set<bitset>*)C_in,(const Set<bitset>*)A_in,(const Set<bitset>*)B_in);
            break;
            default:
            break;
          }
        break;
        default:
        break;
    }

    std::cout << "ERROR" << std::endl;
    return C_in;
  }
}
#endif
