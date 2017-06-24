// g++-7 -g --std=c++14 -pipe -Wall -O3 -ffast-math -fno-finite-math-only -march=native -mavx -fopenmp mandelbrot_avx.cpp

# include "stdafx.h"

#include <cstddef>
#include <cstdio>
#include <chrono>
#include <vector>
#include <tuple>

#include <emmintrin.h>
#include <immintrin.h>

#ifdef _MSVC_LANG
# define MANDEL_INLINE __forceinline
#else
# define MANDEL_INLINE inline
#endif

namespace
{
  constexpr auto    min_x    = -1.5F              ;
  constexpr auto    min_y    = -1.0F              ;
  constexpr auto    max_x    =  0.5F              ;
  constexpr auto    max_y    =  1.0F              ;

  template<typename T>
  auto time_it (T a)
  {
    auto before = std::chrono::high_resolution_clock::now ();
    auto result = a ();
    auto after  = std::chrono::high_resolution_clock::now ();
    auto diff   = std::chrono::duration_cast<std::chrono::milliseconds> (after - before).count ();
    return std::make_tuple (diff, std::move (result));
  }

#define MANDEL_INDEPENDENT(i)                                         \
        xy[i] = _mm256_mul_ps (x[i], y[i]);                           \
        x2[i] = _mm256_mul_ps (x[i], x[i]);                           \
        y2[i] = _mm256_mul_ps (y[i], y[i]);
#define MANDEL_DEPENDENT(i)                                           \
        y[i]  = _mm256_add_ps (_mm256_add_ps (xy[i], xy[i]) , cy[i]); \
        x[i]  = _mm256_add_ps (_mm256_sub_ps (x2[i], y2[i]) , cx[i]);

#define MANDEL_ITERATION()  \
  MANDEL_INDEPENDENT(0)     \
  MANDEL_DEPENDENT(0)       \
  MANDEL_INDEPENDENT(1)     \
  MANDEL_DEPENDENT(1)       \
  MANDEL_INDEPENDENT(2)     \
  MANDEL_DEPENDENT(2)       \
  MANDEL_INDEPENDENT(3)     \
  MANDEL_DEPENDENT(3)

#define MANDEL_CMPMASK()  \
        cmp_mask      =   \
            (_mm256_movemask_ps (_mm256_cmp_ps (_mm256_add_ps (x2[0], y2[0]), _mm256_set1_ps (4.0F), _CMP_LT_OQ))      ) \
          | (_mm256_movemask_ps (_mm256_cmp_ps (_mm256_add_ps (x2[1], y2[1]), _mm256_set1_ps (4.0F), _CMP_LT_OQ)) << 8 ) \
          | (_mm256_movemask_ps (_mm256_cmp_ps (_mm256_add_ps (x2[2], y2[2]), _mm256_set1_ps (4.0F), _CMP_LT_OQ)) << 16) \
          | (_mm256_movemask_ps (_mm256_cmp_ps (_mm256_add_ps (x2[3], y2[3]), _mm256_set1_ps (4.0F), _CMP_LT_OQ)) << 24)

#define MANDEL_INNER()                                    \
        x2 = _mm256_mul_ps  (x, x);                       \
        y2 = _mm256_mul_ps  (y, y);                       \
        r2 = _mm256_add_ps  (x2, y2);                     \
        xy = _mm256_mul_ps  (x, y);                       \
        y  = _mm256_add_ps (_mm256_add_ps (xy, xy) , cy); \
        x  = _mm256_add_ps (_mm256_sub_ps (x2, y2) , cx);

#define MANDEL_CHECK()                                    \
        auto cmp        = _mm256_cmp_ps  (r2, _mm256_set1_ps (4.0), _CMP_LT_OQ);  \
        cmp_mask        = _mm256_movemask_ps (cmp);

  inline int mandelbrot_avx_slow (__m256 cx, __m256 cy, std::size_t max_iter)
  {
    auto x = cx;
    auto y = cy;

    int cmp_mask = 0;

    __m256 x2, y2, xy, r2;

    for (auto outer = 6; outer > 0; --outer)
    {
      MANDEL_INNER()
      MANDEL_INNER()
      MANDEL_INNER()
      MANDEL_INNER()
      MANDEL_INNER()
      MANDEL_INNER()
      MANDEL_INNER()
      MANDEL_INNER()

      MANDEL_CHECK ();

      if (!cmp_mask)
      {
        return 0;
      }

    }

    MANDEL_INNER()
    MANDEL_INNER()

    MANDEL_CHECK ();

    return cmp_mask;
  }

  int mandelbrot_avx (__m256 cx[4], __m256 cy[4])
  {
    auto b0 = mandelbrot_avx_slow(cx[0], cy[0], 50);
    auto b1 = mandelbrot_avx_slow(cx[1], cy[1], 50);
    auto b2 = mandelbrot_avx_slow(cx[2], cy[2], 50);
    auto b3 = mandelbrot_avx_slow(cx[3], cy[3], 50);
    return b0 | (b1 << 8) | (b2 << 16)  | (b2 << 24);
  }

/*
  MANDEL_INLINE int mandelbrot_avx (__m256 cx[4], __m256 cy[4])
  {

    __m256  x[4] {cx[0], cx[1], cx[2], cx[3]};
    __m256  y[4] {cy[0], cy[1], cy[2], cy[3]};
    __m256 x2[4];
    __m256 y2[4];
    __m256 xy[4];

    std::uint32_t cmp_mask;

    // 6 * 8 + 2 => 50 iterations
    auto iter = 6;  // TODO: Should be 7 instead?
    do
    {
      // 8 inner steps
      MANDEL_ITERATION();
      MANDEL_ITERATION();
      MANDEL_ITERATION();
      MANDEL_ITERATION();
      MANDEL_ITERATION();
      MANDEL_ITERATION();
      MANDEL_ITERATION();
      MANDEL_ITERATION();

      MANDEL_CMPMASK();

      if (!cmp_mask)
      {
        return 0;
      }

      --iter;

    } while (iter && cmp_mask);

    // Last 2 steps
    MANDEL_ITERATION();
    MANDEL_ITERATION();

    MANDEL_CMPMASK();

    return cmp_mask;
  }
*/

  std::vector<std::uint8_t> compute_set (std::size_t const dim)
  {
    std::vector<std::uint8_t> set;

    auto width  = dim / 8; // modulo 8 checked earlier

    set.resize (width*dim);

    auto pset   = &set.front ();

    auto sdim   = static_cast<int> (dim);

//    #pragma omp parallel for schedule(guided)
    for (auto sy = 0; sy < sdim; sy += 4)
    {
      auto y      = static_cast<std::size_t> (sy);
      auto scalex = (max_x - min_x) / dim;
      auto scaley = (max_y - min_y) / dim;

      auto incx   = _mm256_set_ps (
          0*scalex
        , 1*scalex
        , 2*scalex
        , 3*scalex
        , 4*scalex
        , 5*scalex
        , 6*scalex
        , 7*scalex
        );

      auto yoffset = width*y;
      for (auto w = 0U; w < width; ++w)
      {
        auto x      = w*8;
        auto cx0    = _mm256_add_ps  (_mm256_set1_ps (scalex*x + min_x), incx);
        auto cy0    = _mm256_set1_ps (scaley*y + min_y);
        auto cy1    = _mm256_add_ps  (cy0, _mm256_set1_ps (1*scaley));
        auto cy2    = _mm256_add_ps  (cy0, _mm256_set1_ps (2*scaley));
        auto cy3    = _mm256_add_ps  (cy0, _mm256_set1_ps (3*scaley));
        __m256 cx[] = { cx0, cx0, cx0, cx0 };
        __m256 cy[] = { cy0, cy1, cy2, cy3 };
        auto bits2  = mandelbrot_avx (cx, cy);
        pset[yoffset           + w] = 0xFF & bits2;
        pset[yoffset + 1*width + w] = 0xFF & (bits2 >> 8);
        pset[yoffset + 2*width + w] = 0xFF & (bits2 >> 16);
        pset[yoffset + 3*width + w] = 0xFF & (bits2 >> 24);
      }
    }

    set.resize (width*dim);

    return set;
  }

}

int main (int argc, char const * argv[])
{
  auto dim  = [argc, argv] ()
  {
    auto dim = argc > 1 ? atoi (argv[1]) : 0;
    return dim > 0 ? dim : 200;
  } ();

  if (dim % 8 != 0)
  {
    std::printf ("Dimension must be modulo 8\n");
    return 999;
  }

  std::printf ("Generating mandelbrot set %dx%d(50)\n", dim, dim);

  auto res  = time_it ([dim] { return compute_set(dim); });

  auto ms   = std::get<0> (res);
  auto& set = std::get<1> (res);

  std::printf ("  it took %lld ms\n", ms);

  auto file = std::fopen ("mandelbrot_avx.pbm", "wb");

  std::fprintf (file, "P4\n%d %d\n", dim, dim);
  std::fwrite (&set.front (), 1, set.size (), file);

  std::fclose (file);

  return 0;
}

