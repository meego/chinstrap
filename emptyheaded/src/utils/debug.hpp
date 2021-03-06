#ifndef DEBUG_H
#define DEBUG_H

#include "common.hpp"

namespace debug{
  //static size_t bitset_length = 0;
  //static size_t pshort_requirement = 16;
  //static double bitset_req = (1.0/256.0);
  struct timeval tim; 
  static double start_clock (){
    gettimeofday(&tim, NULL);  
    return tim.tv_sec+(tim.tv_usec/1000000.0); 
  }

  static inline double stop_clock(double t_in){
    gettimeofday(&tim, NULL);  
    double t2=tim.tv_sec+(tim.tv_usec/1000000.0);  
    return t2 - t_in;
  }
  static double stop_clock(std::string in,double t_in){
    double t2= stop_clock(t_in);
    std::cout << "Time["+in+"]: " << t2 << " s" << std::endl;
    return t2 - t_in;
  }

  static void allocateStack(){
    const rlim_t kStackSize = 64L * 1024L * 1024L;   // min stack size = 64 Mb
    struct rlimit rl;
    int result;

    result = getrlimit(RLIMIT_STACK, &rl);
    if (result == 0){
      if (rl.rlim_cur < kStackSize){
        rl.rlim_cur = kStackSize;
        result = setrlimit(RLIMIT_STACK, &rl);
        if (result != 0){
          fprintf(stderr, "setrlimit returned result = %d\n", result);
        }
      }
    }
  }

  static void _mm256_print_ps(__m256 x) {
    float *data = new float[8];
    _mm256_storeu_ps(&data[0],x);
    for(size_t i =0 ; i < 8; i++){
      std::cout << "Data[" << i << "]: " << data[i] << std::endl;
    }
    delete[] data;
  }
  static void _mm128i_print_shorts(__m128i x) {
    for(size_t i =0 ; i < 8; i++){
      std::cout << "Data[" << i << "]: " << _mm_extract_epi16(x,i) << std::endl;
    }
  }
  static void _mm128i_print(__m128i x) {
    for(size_t i =0 ; i < 4; i++){
      std::cout << "Data[" << i << "]: " << _mm_extract_epi32(x,i) << std::endl;
    }
  }
  static void _mm256i_print(__m256i x) {
    int *data = new int[8];
    _mm256_storeu_si256((__m256i*)&data[0],x);
    for(size_t i =0 ; i < 8; i++){
      std::cout << "Data[" << i << "]: " << data[i] << std::endl;
    }
    delete[] data;
  }
  //Thanks stack overflow.
  static inline float _mm256_reduce_add_ps(__m256 x) {
    /* ( x3+x7, x2+x6, x1+x5, x0+x4 ) */
    const __m128 x128 = _mm_add_ps(_mm256_extractf128_ps(x, 1), _mm256_castps256_ps128(x));
    /* ( -, -, x1+x3+x5+x7, x0+x2+x4+x6 ) */
    const __m128 x64 = _mm_add_ps(x128, _mm_movehl_ps(x128, x128));
    /* ( -, -, -, x0+x1+x2+x3+x4+x5+x6+x7 ) */
    const __m128 x32 = _mm_add_ss(x64, _mm_shuffle_ps(x64, x64, 0x55));
    /* Conversion to float is a no-op on x86-64 */
    return _mm_cvtss_f32(x32);
  }

  static size_t num_bs = 0;
  static size_t num_pshort = 0;
  static size_t num_uint = 0;
  static size_t num_bp = 0;
  static size_t num_v = 0;
  static double bits_per_edge = 0;
  static double bits_per_edge_nometa = 0;

  static size_t num_uint_uint = 0;
  static size_t num_pshort_pshort = 0;
  static size_t num_bs_bs = 0;
  static size_t num_uint_pshort = 0;
  static size_t num_uint_bs = 0;
  static size_t num_pshort_bs = 0;

  static void dump_stats(){
    std::cout << std::endl;
    std::cout << "Num Bitset: " << num_bs << std::endl;
    std::cout << "Num PShort: " << num_pshort
     << std::endl;
    std::cout << "Num Uint: " << num_uint << std::endl;
    std::cout << "Num BP: " << num_bp << std::endl;
    std::cout << "Num V: " << num_v << std::endl;
    std::cout << "Bits per edge (meta): " << bits_per_edge << std::endl;
    std::cout << "Bits per edge (no meta): " << bits_per_edge_nometa << std::endl;

    std::cout << "Num UINT/UINT: " << num_uint_uint << std::endl;
    std::cout << "Num UINT/PSHORT: " << num_uint_pshort << std::endl;
    std::cout << "Num UINT/BS: " << num_uint_bs << std::endl;
    std::cout << "Num PSHORT/PSHORT: " << num_pshort_pshort << std::endl;
    std::cout << "Num PSHORT/BS: " << num_pshort_bs << std::endl;
    std::cout << "Num BS/BS: " << num_bs_bs << std::endl;
  }
}
#endif
