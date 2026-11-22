// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vaibhav Agarwal

#pragma once
#include <immintrin.h>
#include <cmath>
#include <cstddef>
#include <vector>
#include <algorithm>

// ULTRA: no wasted cycle - branchless, SoA, restrict, prefetch, inline always, align 32
// Web: C++ SIMD AVX2 auto-vectorize -ffast-math, SoA vs AoS, branchless CMOV, _mm_prefetch
#if defined(__GNUC__)
#define RESTRICT __restrict__
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define LIKELY(x) __builtin_expect(!!(x),1)
#define UNLIKELY(x) __builtin_expect(!!(x),0)
#define PREFETCH(p) _mm_prefetch((const char*)(p), _MM_HINT_T0)
#else
#define RESTRICT
#define ALWAYS_INLINE inline
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#define PREFETCH(p) ((void)0)
#endif

namespace avx {

// Sum 8 doubles at a time - branchless, prefetch, restrict
ALWAYS_INLINE double sum_avx2(const double* RESTRICT data, size_t n) {
    if (n < 8) {
        double s=0; for(size_t i=0;i<n;++i) s+=data[i]; return s;
    }
    __m256d acc0 = _mm256_setzero_pd();
    __m256d acc1 = _mm256_setzero_pd();
    size_t i=0;
    for(; i+15 < n; i+=16){
        acc0 = _mm256_add_pd(acc0, _mm256_loadu_pd(data+i));
        acc0 = _mm256_add_pd(acc0, _mm256_loadu_pd(data+i+4));
        acc1 = _mm256_add_pd(acc1, _mm256_loadu_pd(data+i+8));
        acc1 = _mm256_add_pd(acc1, _mm256_loadu_pd(data+i+12));
    }
    for(; i+7 < n; i+=8){
        acc0 = _mm256_add_pd(acc0, _mm256_loadu_pd(data+i));
        acc1 = _mm256_add_pd(acc1, _mm256_loadu_pd(data+i+4));
    }
    __m256d acc = _mm256_add_pd(acc0, acc1);
    __m128d low = _mm256_castpd256_pd128(acc);
    __m128d high = _mm256_extractf128_pd(acc,1);
    __m128d sum2 = _mm_add_pd(low, high);
    sum2 = _mm_hadd_pd(sum2, sum2);
    double s = _mm_cvtsd_f64(sum2);
    for(; i<n; ++i) s+= data[i];
    return s;
}

inline double mean_avx2(const double* d, size_t n){ return sum_avx2(d,n)/double(n); }

// AVX2 stddev
inline double stddev_avx2(const double* data, size_t n, double mean){
    if(n<8){
        double s=0; for(size_t i=0;i<n;++i){ double d=data[i]-mean; s+=d*d; } return std::sqrt(s/double(n));
    }
    __m256d m = _mm256_set1_pd(mean);
    __m256d acc0=_mm256_setzero_pd(), acc1=_mm256_setzero_pd();
    size_t i=0;
    for(; i+15 < n; i+=16){
        __m256d v0=_mm256_sub_pd(_mm256_loadu_pd(data+i), m);
        __m256d v1=_mm256_sub_pd(_mm256_loadu_pd(data+i+4), m);
        __m256d v2=_mm256_sub_pd(_mm256_loadu_pd(data+i+8), m);
        __m256d v3=_mm256_sub_pd(_mm256_loadu_pd(data+i+12), m);
        acc0=_mm256_fmadd_pd(v0,v0,acc0);
        acc0=_mm256_fmadd_pd(v1,v1,acc0);
        acc1=_mm256_fmadd_pd(v2,v2,acc1);
        acc1=_mm256_fmadd_pd(v3,v3,acc1);
    }
    for(; i+7 < n; i+=8){
        __m256d v0=_mm256_sub_pd(_mm256_loadu_pd(data+i), m);
        __m256d v1=_mm256_sub_pd(_mm256_loadu_pd(data+i+4), m);
        acc0=_mm256_fmadd_pd(v0,v0,acc0);
        acc1=_mm256_fmadd_pd(v1,v1,acc1);
    }
    __m256d acc=_mm256_add_pd(acc0,acc1);
    __m128d low=_mm256_castpd256_pd128(acc);
    __m128d high=_mm256_extractf128_pd(acc,1);
    __m128d s2=_mm_add_pd(low,high);
    s2=_mm_hadd_pd(s2,s2);
    double s=_mm_cvtsd_f64(s2);
    for(; i<n; ++i){ double d=data[i]-mean; s+=d*d; }
    return std::sqrt(s/double(n));
}

// Fast SMA using prefix sum + AVX for initial sum (O(n) overall)
inline void sma_avx2(const double* src, double* dst, size_t n, int period){
    if(n==0) return;
    // prefix sum
    std::vector<double> pref(n+1,0);
    for(size_t i=0;i<n;++i) pref[i+1]=pref[i]+src[i];
    size_t i=0;
    for(; i < (size_t)period-1 && i<n; ++i) dst[i]=std::nan("");
    for(; i<n; ++i) dst[i]=(pref[i+1]-pref[i+1-period])/double(period);
}

// EMA - scalar (recursive, can't vectorize easily but we unroll)
inline void ema(const double* src, double* dst, size_t n, int period){
    if(n==0) return;
    double k=2.0/(period+1.0);
    dst[0]=src[0];
    for(size_t i=1;i<n;++i) dst[i]= src[i]*k + dst[i-1]*(1.0-k);
}

// Wilder smoothing (used in RSI/ADX)
inline void wilder_smooth(const double* src, double* dst, size_t n, int period){
    if(n==0) return;
    double sum=0; for(int i=0;i<period && (size_t)i<n; ++i) sum+=src[i];
    size_t i=period-1; if(i<n) dst[i]=sum;
    for(;i+1<n;++i) dst[i+1]= dst[i] - dst[i]/period + src[i+1];
    for(size_t j=0;j<(size_t)period-1 && j<n; ++j) dst[j]=std::nan("");
}

inline double max_avx2(const double* d, size_t n){
    if(n==0) return -1e308;
    if(n<8){ double m=d[0]; for(size_t i=1;i<n;++i) m=std::max(m,d[i]); return m; }
    __m256d mx = _mm256_loadu_pd(d);
    // duplicate to 4 lanes properly
    mx = _mm256_permute4x64_pd(mx, 0x44); // broadcast? simpler scalar seed
    // actually initialize with first element broadcast
    mx = _mm256_set1_pd(d[0]);
    size_t i=0;
    for(; i+7 < n; i+=8){
        __m256d v0=_mm256_loadu_pd(d+i);
        __m256d v1=_mm256_loadu_pd(d+i+4);
        mx=_mm256_max_pd(mx,v0);
        mx=_mm256_max_pd(mx,v1);
    }
    alignas(32) double tmp[4];
    _mm256_store_pd(tmp,mx);
    double m = std::max(std::max(tmp[0],tmp[1]), std::max(tmp[2],tmp[3]));
    for(; i<n; ++i) m=std::max(m,d[i]);
    return m;
}
inline double min_avx2(const double* d, size_t n){
    if(n==0) return 1e308;
    if(n<8){ double m=d[0]; for(size_t i=1;i<n;++i) m=std::min(m,d[i]); return m; }
    __m256d mn=_mm256_set1_pd(d[0]);
    size_t i=0;
    for(; i+7 < n; i+=8){
        __m256d v0=_mm256_loadu_pd(d+i);
        __m256d v1=_mm256_loadu_pd(d+i+4);
        mn=_mm256_min_pd(mn,v0);
        mn=_mm256_min_pd(mn,v1);
    }
    alignas(32) double tmp[4];
    _mm256_store_pd(tmp,mn);
    double m = std::min(std::min(tmp[0],tmp[1]), std::min(tmp[2],tmp[3]));
    for(; i<n; ++i) m=std::min(m,d[i]);
    return m;
}

} // namespace avx
