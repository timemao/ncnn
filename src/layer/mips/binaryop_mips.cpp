// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "binaryop_mips.h"

#include <math.h>

#if __mips_msa
#include <msa.h>
#include "msa_mathfun.h"
#endif // __mips_msa

namespace ncnn {

BinaryOp_mips::BinaryOp_mips()
{
#if __mips_msa
    support_packing = true;
#endif // __mips_msa
}

template<typename Op>
static int binary_op_scalar(const Mat& a, float b, Mat& c, const Option& opt)
{
    Op op;

    const int channels = a.c;
    const int size = a.w * a.h * a.d * a.elempack;

    #pragma omp parallel for num_threads(opt.num_threads)
    for (int q = 0; q < channels; q++)
    {
        const float* ptr = a.channel(q);
        float* outptr = c.channel(q);

        int i = 0;
#if __mips_msa
        v4f32 _b = __msa_fill_w_f32(b);
        for (; i + 3 < size; i += 4)
        {
            __builtin_prefetch(ptr + 16);
            v4f32 _p = (v4f32)__msa_ld_w(ptr, 0);
            _p = op(_p, _b);
            __msa_st_w((v4i32)_p, outptr, 0);
            ptr += 4;
            outptr += 4;
        }
#endif // __mips_msa
        for (; i < size; i++)
        {
            *outptr = op(*ptr, b);
            ptr++;
            outptr++;
        }
    }

    return 0;
}

template<typename Op>
static int binary_op_no_broadcast(const Mat& a, const Mat& b, Mat& c, const Option& opt)
{
    Op op;

    const int channels = a.c;
    const int size = a.w * a.h * a.d * a.elempack;

    #pragma omp parallel for num_threads(opt.num_threads)
    for (int q = 0; q < channels; q++)
    {
        const float* ptr = a.channel(q);
        const float* ptr1 = b.channel(q);
        float* outptr = c.channel(q);

        int i = 0;
#if __mips_msa
        for (; i + 3 < size; i += 4)
        {
            __builtin_prefetch(ptr + 16);
            __builtin_prefetch(ptr1 + 16);
            v4f32 _p = (v4f32)__msa_ld_w(ptr, 0);
            v4f32 _p1 = (v4f32)__msa_ld_w(ptr1, 0);
            v4f32 _outp = op(_p, _p1);
            __msa_st_w((v4i32)_outp, outptr, 0);
            ptr += 4;
            ptr1 += 4;
            outptr += 4;
        }
#endif // __mips_msa
        for (; i < size; i++)
        {
            *outptr = op(*ptr, *ptr1);
            ptr += 1;
            ptr1 += 1;
            outptr += 1;
        }
    }

    return 0;
}

template<typename Op>
static int binary_op_broadcast_inner(const Mat& a, const Mat& b, Mat& c, const Option& opt)
{
    Op op;

    int w = a.w;
    int h = a.h;
    int d = a.d;
    int channels = a.c;
    int elempack = a.elempack;

    if (a.dims == 2 && b.dims == 1)
    {
        // type 8
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int y = 0; y < h; y++)
        {
            const float* ptr = a.row(y);
            float* outptr = c.row(y);

            const float _b = b[y];
#if __mips_msa
            v4f32 _b_128 = (elempack == 4) ? (v4f32)__msa_ld_w((const float*)b + y * 4, 0) : __msa_fill_w_f32(_b);
#endif // __mips_msa

            const int size = w * elempack;

            int i = 0;
#if __mips_msa
            for (; i + 3 < size; i += 4)
            {
                __builtin_prefetch(ptr + 16);
                v4f32 _p = (v4f32)__msa_ld_w(ptr, 0);
                v4f32 _outp = op(_p, _b_128);
                __msa_st_w((v4i32)_outp, outptr, 0);
                ptr += 4;
                outptr += 4;
            }
#endif // __mips_msa
            for (; i < size; i++)
            {
                *outptr = op(*ptr, _b);
                ptr += 1;
                outptr += 1;
            }
        }
    }

    if ((a.dims == 3 || a.dims == 4) && b.dims == 1)
    {
        // type 9 11
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q = 0; q < channels; q++)
        {
            const float* ptr = a.channel(q);
            float* outptr = c.channel(q);

            const float _b = b[q];
#if __mips_msa
            v4f32 _b_128 = (elempack == 4) ? (v4f32)__msa_ld_w((const float*)b + q * 4, 0) : __msa_fill_w_f32(_b);
#endif // __mips_msa

            const int size = w * h * d * elempack;

            int i = 0;
#if __mips_msa
            for (; i + 3 < size; i += 4)
            {
                __builtin_prefetch(ptr + 16);
                v4f32 _p = (v4f32)__msa_ld_w(ptr, 0);
                v4f32 _outp = op(_p, _b_128);
                __msa_st_w((v4i32)_outp, outptr, 0);
                ptr += 4;
                outptr += 4;
            }
#endif // __mips_msa
            for (; i < size; i++)
            {
                *outptr = op(*ptr, _b);
                ptr += 1;
                outptr += 1;
            }
        }
    }

    if (a.dims == 3 && b.dims == 2)
    {
        // type 10
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q = 0; q < channels; q++)
        {
            const float* ptr = a.channel(q);
            const float* ptr1 = b.row(q);
            float* outptr = c.channel(q);

            const int size = w * elempack;

            for (int y = 0; y < h; y++)
            {
                const float _b = ptr1[y];
#if __mips_msa
                v4f32 _b_128 = (elempack == 4) ? (v4f32)__msa_ld_w((const float*)ptr1 + y * 4, 0) : __msa_fill_w_f32(_b);
#endif // __mips_msa

                int i = 0;
#if __mips_msa
                for (; i + 3 < size; i += 4)
                {
                    __builtin_prefetch(ptr + 16);
                    v4f32 _p = (v4f32)__msa_ld_w(ptr, 0);
                    v4f32 _outp = op(_p, _b_128);
                    __msa_st_w((v4i32)_outp, outptr, 0);
                    ptr += 4;
                    outptr += 4;
                }
#endif // __mips_msa
                for (; i < size; i++)
                {
                    *outptr = op(*ptr, _b);
                    ptr += 1;
                    outptr += 1;
                }
            }
        }
    }

    if (a.dims == 4 && b.dims == 2)
    {
        // type 12
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q = 0; q < channels; q++)
        {
            const float* ptr = a.channel(q);
            const float* ptr1 = b.row(q);
            float* outptr = c.channel(q);

            const int size = w * h * elempack;

            for (int z = 0; z < d; z++)
            {
                const float _b = ptr1[z];
#if __mips_msa
                v4f32 _b_128 = (elempack == 4) ? (v4f32)__msa_ld_w((const float*)ptr1 + z * 4, 0) : __msa_fill_w_f32(_b);
#endif // __mips_msa

                int i = 0;
#if __mips_msa
                for (; i + 3 < size; i += 4)
                {
                    __builtin_prefetch(ptr + 16);
                    v4f32 _p = (v4f32)__msa_ld_w(ptr, 0);
                    v4f32 _outp = op(_p, _b_128);
                    __msa_st_w((v4i32)_outp, outptr, 0);
                    ptr += 4;
                    outptr += 4;
                }
#endif // __mips_msa
                for (; i < size; i++)
                {
                    *outptr = op(*ptr, _b);
                    ptr += 1;
                    outptr += 1;
                }
            }
        }
    }

    if (a.dims == 4 && b.dims == 3)
    {
        // type 13
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q = 0; q < channels; q++)
        {
            const float* ptr = a.channel(q);
            float* outptr = c.channel(q);

            const int size = w * elempack;

            for (int z = 0; z < d; z++)
            {
                const float* ptr1 = b.channel(q).row(z);

                for (int y = 0; y < h; y++)
                {
                    const float _b = ptr1[y];
#if __mips_msa
                    v4f32 _b_128 = (elempack == 4) ? (v4f32)__msa_ld_w((const float*)ptr1 + y * 4, 0) : __msa_fill_w_f32(_b);
#endif // __mips_msa

                    int i = 0;
#if __mips_msa
                    for (; i + 3 < size; i += 4)
                    {
                        __builtin_prefetch(ptr + 16);
                        v4f32 _p = (v4f32)__msa_ld_w(ptr, 0);
                        v4f32 _outp = op(_p, _b_128);
                        __msa_st_w((v4i32)_outp, outptr, 0);
                        ptr += 4;
                        outptr += 4;
                    }
#endif // __mips_msa
                    for (; i < size; i++)
                    {
                        *outptr = op(*ptr, _b);
                        ptr += 1;
                        outptr += 1;
                    }
                }
            }
        }
    }

    return 0;
}

template<typename Op>
static int binary_op_broadcast_outer(const Mat& a, const Mat& b, Mat& c, const Option& opt)
{
    Op op;

    int w = a.w;
    int h = a.h;
    int d = a.d;
    int channels = a.c;
    int elempack = a.elempack;

    if (a.dims == 2)
    {
        // type 14
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int y = 0; y < h; y++)
        {
            const float* ptr = a.row(y);
            const float* ptr1 = b;
            float* outptr = c.row(y);

#if __mips_msa
            if (elempack == 4)
            {
                for (int x = 0; x < w; x++)
                {
                    __builtin_prefetch(ptr + 16);
                    v4f32 _p = (v4f32)__msa_ld_w(ptr, 0);
                    v4f32 _b = __msa_fill_w_f32(*ptr1);
                    v4f32 _outp = op(_p, _b);
                    __msa_st_w((v4i32)_outp, outptr, 0);
                    ptr += 4;
                    ptr1 += 1;
                    outptr += 4;
                }
            }
#endif // __mips_msa
            if (elempack == 1)
            {
                for (int x = 0; x < w; x++)
                {
                    *outptr = op(*ptr, *ptr1);
                    ptr += 1;
                    ptr1 += 1;
                    outptr += 1;
                }
            }
        }
    }

    if (a.dims == 3 || a.dims == 4)
    {
        // type 15 16 17 18 19
        #pragma omp parallel for num_threads(opt.num_threads)
        for (int q = 0; q < channels; q++)
        {
            const float* ptr = a.channel(q);
            float* outptr = c.channel(q);

            for (int z = 0; z < d; z++)
            {
                int z1 = std::min(z, b.d - 1);
                for (int y = 0; y < h; y++)
                {
                    int y1 = std::min(y, b.h - 1);

                    const float* ptr1 = b.depth(z1).row(y1);

#if __mips_msa
                    if (elempack == 4)
                    {
                        for (int x = 0; x < w; x++)
                        {
                            __builtin_prefetch(ptr + 16);
                            v4f32 _p = (v4f32)__msa_ld_w(ptr, 0);
                            v4f32 _b = __msa_fill_w_f32(*ptr1);
                            v4f32 _outp = op(_p, _b);
                            __msa_st_w((v4i32)_outp, outptr, 0);
                            ptr += 4;
                            ptr1 += 1;
                            outptr += 4;
                        }
                    }
#endif // __mips_msa
                    if (elempack == 1)
                    {
                        for (int x = 0; x < w; x++)
                        {
                            *outptr = op(*ptr, *ptr1);
                            ptr += 1;
                            ptr1 += 1;
                            outptr += 1;
                        }
                    }
                }
            }
        }
    }

    return 0;
}

template<typename Op>
static int binary_op_broadcast_20(const Mat& a, const Mat& b, Mat& c, const Option& opt)
{
    Op op;

    int w = a.w;
    int h = a.h;
    int channels = a.c;
    int elempack = a.elempack;

    #pragma omp parallel for num_threads(opt.num_threads)
    for (int q = 0; q < channels; q++)
    {
        const float* ptr = a.channel(q);
        float* outptr = c.channel(q);

        for (int y = 0; y < h; y++)
        {
            const float* ptr1 = b.channel(q);

            const int size = w * elempack;

            int i = 0;
#if __mips_msa
            for (; i + 3 < size; i += 4)
            {
                __builtin_prefetch(ptr + 16);
                __builtin_prefetch(ptr1 + 16);
                v4f32 _p = (v4f32)__msa_ld_w(ptr, 0);
                v4f32 _p1 = (v4f32)__msa_ld_w(ptr1, 0);
                v4f32 _outp = op(_p, _p1);
                __msa_st_w((v4i32)_outp, outptr, 0);
                ptr += 4;
                ptr1 += 4;
                outptr += 4;
            }
#endif // __mips_msa
            for (; i < size; i++)
            {
                *outptr = op(*ptr, *ptr1);
                ptr += 1;
                ptr1 += 1;
                outptr += 1;
            }
        }
    }

    return 0;
}

template<typename Op>
static int binary_op_scalar_inplace(Mat& a, float b, const Option& opt)
{
    Op op;

    const int channels = a.c;
    const int size = a.w * a.h * a.d * a.elempack;

    #pragma omp parallel for num_threads(opt.num_threads)
    for (int q = 0; q < channels; q++)
    {
        float* ptr = a.channel(q);

        int i = 0;
#if __mips_msa
        v4f32 _b = __msa_fill_w_f32(b);
        for (; i + 3 < size; i += 4)
        {
            __builtin_prefetch(ptr + 16);
            v4f32 _p = (v4f32)__msa_ld_w(ptr, 0);
            _p = op(_p, _b);
            __msa_st_w((v4i32)_p, ptr, 0);
            ptr += 4;
        }
#endif // __mips_msa
        for (; i < size; i++)
        {
            *ptr = op(*ptr, b);
            ptr++;
        }
    }

    return 0;
}

namespace BinaryOp_mips_functor {

#if __mips_msa
#define MAKE_FUNCTION(NAME, IMPL, IMPL4)                       \
    struct NAME                                                \
    {                                                          \
        float operator()(const float& x, const float& y) const \
        {                                                      \
            return IMPL;                                       \
        }                                                      \
        v4f32 operator()(const v4f32& x, const v4f32& y) const \
        {                                                      \
            return IMPL4;                                      \
        }                                                      \
    };
#else
#define MAKE_FUNCTION(NAME, IMPL, IMPL4)                       \
    struct NAME                                                \
    {                                                          \
        float operator()(const float& x, const float& y) const \
        {                                                      \
            return IMPL;                                       \
        }                                                      \
    };
#endif // __mips_msa

// clang-format off
// *INDENT-OFF*
MAKE_FUNCTION(binary_op_add, x + y, __msa_fadd_w(x, y))
MAKE_FUNCTION(binary_op_sub, x - y, __msa_fsub_w(x, y))
MAKE_FUNCTION(binary_op_mul, x * y, __msa_fmul_w(x, y))
MAKE_FUNCTION(binary_op_div, x / y, __msa_fdiv_w(x, y))
MAKE_FUNCTION(binary_op_max, std::max(x, y), __msa_fmax_w(x, y))
MAKE_FUNCTION(binary_op_min, std::min(x, y), __msa_fmin_w(x, y))
MAKE_FUNCTION(binary_op_pow, (float)pow(x, y), pow_ps(x, y))
MAKE_FUNCTION(binary_op_rsub, y - x, __msa_fsub_w(y, x))
MAKE_FUNCTION(binary_op_rdiv, y / x, __msa_fdiv_w(y, x))
MAKE_FUNCTION(binary_op_rpow, (float)pow(y, x), pow_ps(y, x))
// *INDENT-ON*
// clang-format on

#undef MAKE_FUNCTION

} // namespace BinaryOp_mips_functor

static int binary_op_scalar(const Mat& a, float b, Mat& c, int op_type, const Option& opt)
{
    using namespace BinaryOp_mips_functor;

    if (op_type == BinaryOp::Operation_ADD) return binary_op_scalar<binary_op_add>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_SUB) return binary_op_scalar<binary_op_sub>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_MUL) return binary_op_scalar<binary_op_mul>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_DIV) return binary_op_scalar<binary_op_div>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_MAX) return binary_op_scalar<binary_op_max>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_MIN) return binary_op_scalar<binary_op_min>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_POW) return binary_op_scalar<binary_op_pow>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_RSUB) return binary_op_scalar<binary_op_rsub>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_RDIV) return binary_op_scalar<binary_op_rdiv>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_RPOW) return binary_op_scalar<binary_op_rpow>(a, b, c, opt);

    // should never reach here
    return 0;
}

static int binary_op_no_broadcast(const Mat& a, const Mat& b, Mat& c, int op_type, const Option& opt)
{
    using namespace BinaryOp_mips_functor;

    if (op_type == BinaryOp::Operation_ADD) return binary_op_no_broadcast<binary_op_add>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_SUB) return binary_op_no_broadcast<binary_op_sub>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_MUL) return binary_op_no_broadcast<binary_op_mul>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_DIV) return binary_op_no_broadcast<binary_op_div>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_MAX) return binary_op_no_broadcast<binary_op_max>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_MIN) return binary_op_no_broadcast<binary_op_min>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_POW) return binary_op_no_broadcast<binary_op_pow>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_RSUB) return binary_op_no_broadcast<binary_op_rsub>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_RDIV) return binary_op_no_broadcast<binary_op_rdiv>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_RPOW) return binary_op_no_broadcast<binary_op_rpow>(a, b, c, opt);

    // should never reach here
    return 0;
}

static int binary_op_broadcast_inner(const Mat& a, const Mat& b, Mat& c, int op_type, const Option& opt)
{
    // squeeze inner axes
    Mat b2 = b;
    if (b.dims == 2 && b.w == 1)
        b2 = b.reshape(b.h);
    else if (b.dims == 3 && b.h == 1)
        b2 = b.reshape(b.c);
    else if (b.dims == 3 && b.w == 1)
        b2 = b.reshape(b.h, b.c);
    else if (b.dims == 4 && b.d == 1)
        b2 = b.reshape(b.c);
    else if (b.dims == 4 && b.h == 1)
        b2 = b.reshape(b.d, b.c);
    else if (b.dims == 4 && b.w == 1)
        b2 = b.reshape(b.h, b.d, b.c);

    using namespace BinaryOp_mips_functor;

    if (op_type == BinaryOp::Operation_ADD) return binary_op_broadcast_inner<binary_op_add>(a, b2, c, opt);
    if (op_type == BinaryOp::Operation_SUB) return binary_op_broadcast_inner<binary_op_sub>(a, b2, c, opt);
    if (op_type == BinaryOp::Operation_MUL) return binary_op_broadcast_inner<binary_op_mul>(a, b2, c, opt);
    if (op_type == BinaryOp::Operation_DIV) return binary_op_broadcast_inner<binary_op_div>(a, b2, c, opt);
    if (op_type == BinaryOp::Operation_MAX) return binary_op_broadcast_inner<binary_op_max>(a, b2, c, opt);
    if (op_type == BinaryOp::Operation_MIN) return binary_op_broadcast_inner<binary_op_min>(a, b2, c, opt);
    if (op_type == BinaryOp::Operation_POW) return binary_op_broadcast_inner<binary_op_pow>(a, b2, c, opt);
    if (op_type == BinaryOp::Operation_RSUB) return binary_op_broadcast_inner<binary_op_rsub>(a, b2, c, opt);
    if (op_type == BinaryOp::Operation_RDIV) return binary_op_broadcast_inner<binary_op_rdiv>(a, b2, c, opt);
    if (op_type == BinaryOp::Operation_RPOW) return binary_op_broadcast_inner<binary_op_rpow>(a, b2, c, opt);

    // should never reach here
    return 0;
}

static int binary_op_broadcast_outer(const Mat& a, const Mat& b, Mat& c, int op_type, const Option& opt)
{
    using namespace BinaryOp_mips_functor;

    if (op_type == BinaryOp::Operation_ADD) return binary_op_broadcast_outer<binary_op_add>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_SUB) return binary_op_broadcast_outer<binary_op_sub>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_MUL) return binary_op_broadcast_outer<binary_op_mul>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_DIV) return binary_op_broadcast_outer<binary_op_div>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_MAX) return binary_op_broadcast_outer<binary_op_max>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_MIN) return binary_op_broadcast_outer<binary_op_min>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_POW) return binary_op_broadcast_outer<binary_op_pow>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_RSUB) return binary_op_broadcast_outer<binary_op_rsub>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_RDIV) return binary_op_broadcast_outer<binary_op_rdiv>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_RPOW) return binary_op_broadcast_outer<binary_op_rpow>(a, b, c, opt);

    // should never reach here
    return 0;
}

static int binary_op_broadcast_20(const Mat& a, const Mat& b, Mat& c, int op_type, const Option& opt)
{
    using namespace BinaryOp_mips_functor;

    if (op_type == BinaryOp::Operation_ADD) return binary_op_broadcast_20<binary_op_add>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_SUB) return binary_op_broadcast_20<binary_op_sub>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_MUL) return binary_op_broadcast_20<binary_op_mul>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_DIV) return binary_op_broadcast_20<binary_op_div>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_MAX) return binary_op_broadcast_20<binary_op_max>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_MIN) return binary_op_broadcast_20<binary_op_min>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_POW) return binary_op_broadcast_20<binary_op_pow>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_RSUB) return binary_op_broadcast_20<binary_op_rsub>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_RDIV) return binary_op_broadcast_20<binary_op_rdiv>(a, b, c, opt);
    if (op_type == BinaryOp::Operation_RPOW) return binary_op_broadcast_20<binary_op_rpow>(a, b, c, opt);

    // should never reach here
    return 0;
}

static int get_reverse_op_type(int op_type)
{
    if (op_type == BinaryOp::Operation_SUB) return BinaryOp::Operation_RSUB;
    if (op_type == BinaryOp::Operation_DIV) return BinaryOp::Operation_RDIV;
    if (op_type == BinaryOp::Operation_POW) return BinaryOp::Operation_RPOW;
    if (op_type == BinaryOp::Operation_RSUB) return BinaryOp::Operation_SUB;
    if (op_type == BinaryOp::Operation_RDIV) return BinaryOp::Operation_DIV;
    if (op_type == BinaryOp::Operation_RPOW) return BinaryOp::Operation_POW;
    return op_type;
}

int BinaryOp_mips::forward(const std::vector<Mat>& bottom_blobs, std::vector<Mat>& top_blobs, const Option& opt) const
{
    const bool b_is_scalar = bottom_blobs[1].w * bottom_blobs[1].h * bottom_blobs[1].d * bottom_blobs[1].c * bottom_blobs[1].elempack == 1;
    const bool a_rank_is_lower = bottom_blobs[0].dims < bottom_blobs[1].dims && !b_is_scalar;
    const bool a_size_is_lower = bottom_blobs[0].w * bottom_blobs[0].h * bottom_blobs[0].d * bottom_blobs[0].c * bottom_blobs[0].elempack < bottom_blobs[1].w * bottom_blobs[1].h * bottom_blobs[1].d * bottom_blobs[1].c * bottom_blobs[1].elempack;
    const bool a_is_lower = a_rank_is_lower || (!a_rank_is_lower && a_size_is_lower);
    const Mat& A = a_is_lower ? bottom_blobs[1] : bottom_blobs[0];
    const Mat& B = a_is_lower ? bottom_blobs[0] : bottom_blobs[1];
    const int op_type_r = a_is_lower ? get_reverse_op_type(op_type) : op_type;

    Mat& top_blob = top_blobs[0];
    top_blob.create_like(A, opt.blob_allocator);
    if (top_blob.empty())
        return -100;

    // B is a scalar
    if (B.w * B.h * B.d * B.c * B.elempack == 1)
    {
        return binary_op_scalar(A, B[0], top_blob, op_type_r, opt);
    }

    // no broadcast
    if (A.dims == B.dims && A.w == B.w && A.h == B.h && A.d == B.d && A.c == B.c && A.elempack == B.elempack)
    {
        return binary_op_no_broadcast(A, B, top_blob, op_type_r, opt);
    }

    // broadcast B for inner axis
    if ((B.dims < A.dims)
            || (A.dims == 2 && B.w == 1 && B.h == A.h)
            || (A.dims == 3 && B.w == 1 && B.h == 1 && B.c == A.c)
            || (A.dims == 3 && B.w == 1 && B.h == A.h && B.c == A.c)
            || (A.dims == 4 && B.w == 1 && B.h == 1 && B.d == 1 && B.c == A.c)
            || (A.dims == 4 && B.w == 1 && B.h == 1 && B.d == A.d && B.c == A.c)
            || (A.dims == 4 && B.w == 1 && B.h == A.h && B.d == A.d && B.c == A.c))
    {
        return binary_op_broadcast_inner(A, B, top_blob, op_type_r, opt);
    }

    // broadcast B for outer axis
    if (B.elempack == 1 && ((A.dims == 2 && B.w == A.w && B.h == 1) || (A.dims == 3 && B.w == A.w && B.h == 1 && B.c == 1) || (A.dims == 3 && B.w == A.w && B.h == A.h && B.c == 1) || (A.dims == 4 && B.w == A.w && B.h == 1 && B.d == 1 && B.c == 1) || (A.dims == 4 && B.w == A.w && B.h == A.h && B.d == 1 && B.c == 1) || (A.dims == 4 && B.w == A.w && B.h == A.h && B.d == A.d && B.c == 1)))
    {
        return binary_op_broadcast_outer(A, B, top_blob, op_type_r, opt);
    }

    // some special broadcast rule here
    if (A.dims == 3 && B.dims == 3 && A.w == B.w && B.h == 1 && A.c == B.c)
    {
        return binary_op_broadcast_20(A, B, top_blob, op_type_r, opt);
    }

    return 0;
}

int BinaryOp_mips::forward_inplace(Mat& bottom_top_blob, const Option& opt) const
{
    using namespace BinaryOp_mips_functor;

    if (op_type == Operation_ADD) return binary_op_scalar_inplace<binary_op_add>(bottom_top_blob, b, opt);
    if (op_type == Operation_SUB) return binary_op_scalar_inplace<binary_op_sub>(bottom_top_blob, b, opt);
    if (op_type == Operation_MUL) return binary_op_scalar_inplace<binary_op_mul>(bottom_top_blob, b, opt);
    if (op_type == Operation_DIV) return binary_op_scalar_inplace<binary_op_div>(bottom_top_blob, b, opt);
    if (op_type == Operation_MAX) return binary_op_scalar_inplace<binary_op_max>(bottom_top_blob, b, opt);
    if (op_type == Operation_MIN) return binary_op_scalar_inplace<binary_op_min>(bottom_top_blob, b, opt);
    if (op_type == Operation_POW) return binary_op_scalar_inplace<binary_op_pow>(bottom_top_blob, b, opt);
    if (op_type == Operation_RSUB) return binary_op_scalar_inplace<binary_op_rsub>(bottom_top_blob, b, opt);
    if (op_type == Operation_RDIV) return binary_op_scalar_inplace<binary_op_rdiv>(bottom_top_blob, b, opt);
    if (op_type == Operation_RPOW) return binary_op_scalar_inplace<binary_op_rpow>(bottom_top_blob, b, opt);

    return 0;
}

} // namespace ncnn
