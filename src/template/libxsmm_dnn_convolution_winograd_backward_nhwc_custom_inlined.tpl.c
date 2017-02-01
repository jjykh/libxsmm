/******************************************************************************
** Copyright (c) 2016-2017, Intel Corporation                                **
** All rights reserved.                                                      **
**                                                                           **
** Redistribution and use in source and binary forms, with or without        **
** modification, are permitted provided that the following conditions        **
** are met:                                                                  **
** 1. Redistributions of source code must retain the above copyright         **
**    notice, this list of conditions and the following disclaimer.          **
** 2. Redistributions in binary form must reproduce the above copyright      **
**    notice, this list of conditions and the following disclaimer in the    **
**    documentation and/or other materials provided with the distribution.   **
** 3. Neither the name of the copyright holder nor the names of its          **
**    contributors may be used to endorse or promote products derived        **
**    from this software without specific prior written permission.          **
**                                                                           **
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       **
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT         **
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR     **
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT      **
** HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,    **
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED  **
** TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR    **
** PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    **
** LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING      **
** NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS        **
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              **
******************************************************************************/
/* Kunal Banerjee (Intel Corp.)
******************************************************************************/

  int ltid;
  int work;
  int chunksize;
  int thr_begin;
  int thr_end;
  int job;
  int img;
  int img1;
  int ifm1;
  int ifm2;
  int ofm1;
  int ofm2;
  int oj;
  int oi;
  unsigned int ti, tj;
  unsigned int i, j, k, l;
  LIBXSMM_ASSUME_ALIGNED(handle->reg_input->data,  64);
  LIBXSMM_ASSUME_ALIGNED(handle->reg_output->data, 64);
  LIBXSMM_ASSUME_ALIGNED(handle->reg_filter->data, 64);

  LIBXSMM_VLA_DECL(5, float, input, handle->reg_input->data, handle->ifhp, handle->ifwp, handle->blocksifm, TDVLEN);
  LIBXSMM_VLA_DECL(5, float, output, handle->reg_output->data, handle->ofhp, handle->ofwp, handle->blocksofm, TDVLEN);
  LIBXSMM_VLA_DECL(6, float, weight, handle->reg_filter->data, handle->blocksifm, handle->desc.R, handle->desc.S, TDVLEN, TDVLEN);
  /*LIBXSMM_VLA_DECL(2, float, bias, handle->bias->data, TDVLEN);*/

  float *up = handle->scratch1; /*(float*)libxsmm_aligned_malloc(ALPHA*ALPHA*handle->desc.C*handle->desc.K*sizeof(float), 64);*/
  float *vp = handle->scratch3; /*(float*)libxsmm_aligned_malloc(ALPHA*ALPHA*handle->cwino_bwd.itiles*handle->cwino_bwd.jtiles*handle->desc.C*handle->desc.N*sizeof(float), 64);*/
  float *mp = handle->scratch4; /*(float*)libxsmm_aligned_malloc(ALPHA*ALPHA*handle->cwino_bwd.itiles*handle->cwino_bwd.jtiles*handle->desc.K*handle->desc.N*sizeof(float), 64);*/

  LIBXSMM_VLA_DECL(6, float, U, up, ALPHA, handle->blocksifm/VRATIO, handle->blocksofm/VRATIO, FDVLEN, FDVLEN);
  LIBXSMM_VLA_DECL(8, float, V, vp, ALPHA, ALPHA, handle->blocksifm/VRATIO, handle->cwino_bwd.bimg, handle->cwino_bwd.jtiles, handle->cwino_bwd.itiles, FDVLEN);
  LIBXSMM_VLA_DECL(8, float, M, mp, ALPHA, ALPHA, handle->blocksofm/VRATIO, handle->cwino_bwd.bimg, handle->cwino_bwd.jtiles, handle->cwino_bwd.itiles, FDVLEN);

  typedef libxsmm_sconvfunction libxsmm_convfunction;
  libxsmm_convfunction jitted_conv_bp;
  jitted_conv_bp = (libxsmm_convfunction)handle->code_bwd[1].xconv.sconv;

  /* computing first logical thread */
  ltid = tid - start_thread;
  libxsmm_barrier_init((libxsmm_barrier*)handle->barrier, ltid);

/* #define BTIME */
#ifdef BTIME
  unsigned long long t_input  = 0;
  unsigned long long t_wt     = 0;
  unsigned long long t_output = 0;
  unsigned long long t_gemm   = 0;
  unsigned long long t_start  = 0;
#endif

  /* number of tasks that could be run in parallel */
  work = handle->desc.N*(handle->blocksofm/VRATIO);
  /* compute chunck size */
  chunksize = (work % handle->desc.threads == 0) ? (work / handle->desc.threads) : (work / handle->desc.threads) + 1;
  /* compute thr_begin and thr_end */
  thr_begin = (ltid * work) / handle->desc.threads;
  thr_end = ((ltid+1) * work) / handle->desc.threads;

#ifdef BTIME
  t_start = __rdtsc();
#endif
  for (job = thr_begin; job < thr_end; job++) {
    img  = job / (handle->blocksofm / VRATIO);
    ofm1 = (job % (handle->blocksofm / VRATIO)) * VRATIO;
    internal_bwd_input_transform_nhwc_custom(&LIBXSMM_VLA_ACCESS(5, output, img, 0, 0, ofm1, 0, handle->ofhp, handle->ofwp, handle->blocksofm, TDVLEN),
      &LIBXSMM_VLA_ACCESS(8, M, img/handle->cwino_bwd.bimg, 0, 0, ofm1/VRATIO, img%handle->cwino_bwd.bimg, 0, 0, 0, ALPHA, ALPHA, handle->blocksofm/VRATIO, handle->cwino_bwd.bimg, handle->cwino_bwd.jtiles, handle->cwino_bwd.itiles, FDVLEN), handle);
  }
#ifdef BTIME
  libxsmm_barrier_wait((libxsmm_barrier*)handle->barrier, ltid);
  t_input = __rdtsc() - t_start;
#endif

  /* number of tasks that could be run in parallel */
  work = (handle->blocksofm/VRATIO)*(handle->blocksifm/VRATIO);
  /* compute chunck size */
  chunksize = (work % handle->desc.threads == 0) ? (work / handle->desc.threads) : (work / handle->desc.threads) + 1;
  /* compute thr_begin and thr_end */
  thr_begin = (ltid * chunksize < work) ? (ltid * chunksize) : work;
  thr_end = ((ltid + 1) * chunksize < work) ? ((ltid + 1) * chunksize) : work;

#ifdef BTIME
  t_start = __rdtsc();
#endif
  for (job = thr_begin; job < thr_end; job++) {
    ofm1 = (job / (handle->blocksifm / VRATIO)) * VRATIO;
    ifm1 = (job % (handle->blocksifm / VRATIO)) * VRATIO;
    internal_bwd_weight_transform(&LIBXSMM_VLA_ACCESS(6, weight, ofm1, ifm1, 0, 0, 0, 0, handle->blocksifm, handle->desc.R, handle->desc.S, TDVLEN, TDVLEN),
      &LIBXSMM_VLA_ACCESS(6, U, 0, 0, ifm1/VRATIO, ofm1/VRATIO, 0, 0, ALPHA, handle->blocksifm/VRATIO, handle->blocksofm/VRATIO, FDVLEN, FDVLEN), handle);
  }
  libxsmm_barrier_wait((libxsmm_barrier*)handle->barrier, ltid);
#ifdef BTIME
  t_wt = __rdtsc() - t_start;
#endif

  /* number of tasks that could be run in parallel */
  work = (handle->desc.N/handle->cwino_bwd.bimg) * ALPHA * ALPHA;
  /* compute chunck size */
  chunksize = (work % handle->desc.threads == 0) ? (work / handle->desc.threads) : (work / handle->desc.threads) + 1;
  /* compute thr_begin and thr_end */
  thr_begin = (ltid * chunksize < work) ? (ltid * chunksize) : work;
  thr_end = ((ltid + 1) * chunksize < work) ? ((ltid + 1) * chunksize) : work;

#ifdef BTIME
  t_start = __rdtsc();
#endif
  for (job = thr_begin; job < thr_end; job++) {
    img = job / (ALPHA * ALPHA);
    oj = (job % (ALPHA * ALPHA)) / ALPHA;
    oi = (job % (ALPHA * ALPHA)) % ALPHA;
    for (ifm1 = 0; ifm1 < handle->blocksifm/VRATIO; ifm1++) {
      for (i = 0; i < handle->cwino_bwd.bimg; i++) {
        for (j = 0; j < handle->cwino_bwd.jtiles; j++) {
          for (k = 0; k < handle->cwino_bwd.itiles; k++) {
            LIBXSMM_PRAGMA_SIMD
            for (l = 0; l < FDVLEN; l++) {
              V[img][oj][oi][ifm1][i][j][k][l] = 0.0f;
            }
          }
        }
      }
      for (ofm1 = 0; ofm1 < handle->blocksofm/VRATIO; ofm1++) {
#if 1
        jitted_conv_bp((const float*)&(U[oj][oi][ifm1][ofm1][0][0]), (const float*)&(M[img][oj][oi][ofm1][0][0][0][0]), (float*)&(V[img][oj][oi][ifm1][0][0][0][0]), 0, 0, 0);
#else
        for (img1 = 0; img1 < handle->cwino_bwd.bimg; img1++) {
          for (tj = 0; tj < handle->cwino_bwd.jtiles; tj++) {
            for (ti = 0; ti < handle->cwino_bwd.itiles; ti++) {
              for (ofm2 = 0; ofm2 < FDVLEN; ofm2++) {
                for (ifm2 = 0; ifm2 < FDVLEN; ifm2++) {
                  V[img][oj][oi][ifm1][img1][tj][ti][ifm2] += M[img][oj][oi][ofm1][img1][tj][ti][ofm2] * U[oj][oi][ifm1][ofm1][ofm2][ifm2];
                }
              }
            }
          }
        }
#endif
      }
    }
  }
  libxsmm_barrier_wait((libxsmm_barrier*)handle->barrier, ltid);
#ifdef BTIME
  t_gemm = __rdtsc() - t_start;
#endif

  /* number of tasks that could be run in parallel */
  work = handle->desc.N*(handle->blocksifm/VRATIO);
  /* compute chunck size */
  chunksize = (work % handle->desc.threads == 0) ? (work / handle->desc.threads) : (work / handle->desc.threads) + 1;
  /* compute thr_begin and thr_end */
  thr_begin = (ltid * chunksize < work) ? (ltid * chunksize) : work;
  thr_end = ((ltid + 1) * chunksize < work) ? ((ltid + 1) * chunksize) : work;

#ifdef BTIME
  t_start = __rdtsc();
#endif
  for (job = thr_begin; job < thr_end; job++) {
    img  = job / (handle->blocksifm / VRATIO);
    ifm1 = (job % (handle->blocksifm / VRATIO)) * VRATIO;
    internal_bwd_output_transform_nhwc_custom(&LIBXSMM_VLA_ACCESS(8, V, img/handle->cwino_bwd.bimg, 0, 0, ifm1/VRATIO, img%handle->cwino_bwd.bimg, 0, 0, 0, ALPHA, ALPHA, handle->blocksifm/VRATIO, handle->cwino_bwd.bimg, handle->cwino_bwd.jtiles, handle->cwino_bwd.itiles, FDVLEN),
      &LIBXSMM_VLA_ACCESS(5, input, img, 0, 0, ifm1, 0, handle->ifhp, handle->ifwp, handle->blocksifm, TDVLEN), handle);
  }
  libxsmm_barrier_wait((libxsmm_barrier*)handle->barrier, ltid);
#ifdef BTIME
  t_output = __rdtsc() - t_start;
#endif

#ifdef BTIME
  if (tid == 0) {
    int nOfm = handle->blocksofm*TDVLEN;
    int nIfm = handle->blocksifm*TDVLEN;
    double b_input = 1.0*handle->desc.N*nIfm*(handle->ifhp*handle->ifwp + handle->cwino_bwd.jtiles*handle->cwino_bwd.itiles*ALPHA*ALPHA) * sizeof(float);
    double b_wt    = 1.0*nOfm*nIfm*(handle->desc.R*handle->desc.S + ALPHA*ALPHA) * sizeof(float);
    double b_output= 1.0*handle->desc.N*nOfm*(handle->ofhp*handle->ofwp + handle->cwino_bwd.jtiles*handle->cwino_bwd.itiles*ALPHA*ALPHA) * sizeof(float);
    double f_gemm = 2.0*handle->desc.N*nOfm*nIfm*handle->cwino_bwd.jtiles*handle->cwino_bwd.itiles*ALPHA*ALPHA;
    printf("Time: i=%8.3f  w=%8.3f  o=%8.3f         g=%8.3f\n", t_input/1000.0, t_wt/1000.0, t_output/1000.0, t_gemm/1000.0);
    printf("BW:   i=%8.3f  w=%8.3f  o=%8.3f (b/c)   g=%8.3f (f/c)\n\n", b_output/t_input, b_wt/t_wt, b_input/t_output, f_gemm/t_gemm);
  }
#endif