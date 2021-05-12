/*
 * coarse_l1_blas.cpp
 *
 *  Created on: Dec 12, 2016
 *      Author: bjoo
 */
#include "lattice/coarse/coarse_l1_blas.h"
#include "lattice/cmat_mult.h"
#include "lattice/constants.h"
#include "lattice/lattice_info.h"
#include "utils/timer.h"
#include <algorithm>
#include <cassert>
#include <complex>

#include "MG_config.h"

#ifdef MG_QMP_COMMS
#    include <qmp.h>
#endif
// for random numbers:
#include <random>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace MG {

    namespace GlobalComm {

#ifdef MG_QMP_COMMS
        void GlobalSum(std::vector<double> &array, const CoarseSpinor &ref) {
            Timer::TimerAPI::startTimer("CoarseSpinor/globalsum/sp" +
                                        std::to_string(ref.GetNumColorSpin()));
            QMP_sum_double_array(&array[0], array.size());
            Timer::TimerAPI::stopTimer("CoarseSpinor/globalsum/sp" +
                                       std::to_string(ref.GetNumColorSpin()));
        }
        void GlobalSum(std::vector<std::complex<double>> &array, const CoarseSpinor &ref) {
            Timer::TimerAPI::startTimer("CoarseSpinor/globalsum/sp" +
                                        std::to_string(ref.GetNumColorSpin()));
            QMP_sum_double_array((double *)&array[0], array.size() * 2);
            Timer::TimerAPI::stopTimer("CoarseSpinor/globalsum/sp" +
                                       std::to_string(ref.GetNumColorSpin()));
        }
#else
        void GlobalSum(std::vector<double> &array) {
            return; // Single Node for now. Return the untouched array. -- MPI Version should use allreduce
        }

        void GlobalSum(std::vector<std::complex<double>> &array) {
            return; // Single Node for now. Return the untouched array. -- MPI Version should use allreduce
        }
#endif
    }

  template <typename T> class multivector {
    const std::size_t n;
    const int threads;
    T* const v;
    
    public:
    multivector(std::size_t n) : n(n), 
#ifdef _OPENMP
     threads(omp_get_max_threads()),
#else
     threads(1),
#endif
     v(new T[n * threads])
    {
       for (std::size_t i=0; i<n*threads; i++) v[i] = T{0};
    }

    ~multivector() { delete [] v; }

    T* mine() const {
#ifdef _OPENMP
      int this_thread = 0;
#else
      int this_thread = omp_get_thread_num();
#endif
      return &v[this_thread * n];
    }

    std::vector<T> reduce() {
       std::vector<T> r(n);
       for (std::size_t i=0; i<n; i++)
          for (int j=0; j<threads; j++) r[j] += v[i*threads + j];
       return r;
    }
  };

    /** Performs:
 *  x <- x - y;
 *  returns: norm(x) after subtraction
 *  Useful for computing residua, where r = b and y = Ax
 *  then n2 = xmyNorm(r,y); will leave r as the residuum and return its square norm
 *
 * @param x  - CoarseSpinor ref
 * @param y  - CoarseSpinor ref
 * @return   double containing the square norm of the difference
 *
 */
    std::vector<double> XmyNorm2Vec(CoarseSpinor &x, const CoarseSpinor &y,
                                    const CBSubset &subset) {
        const LatticeInfo &x_info = x.GetInfo();
        const LatticeInfo &y_info = y.GetInfo();
        AssertCompatible(x_info, y_info);

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();

        multivector<double> norm_diffm(ncol);

        // Loop over the sites and sum up the norm
#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {
                    double *norm_diff_ptr = norm_diffm.mine();

                    // Identify the site and the column
                    float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);
                    const float *y_site_data = y.GetSiteDataPtr(col, cb, cbsite);

                    // Sum difference over the colorspins
                    double cspin_sum = 0;
#pragma omp simd reduction(+ : cspin_sum)
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {
                        float diff_re = x_site_data[RE + n_complex * cspin] -
                                        y_site_data[RE + n_complex * cspin];
                        float diff_im = x_site_data[IM + n_complex * cspin] -
                                        y_site_data[IM + n_complex * cspin];
                        x_site_data[RE + n_complex * cspin] = diff_re;
                        x_site_data[IM + n_complex * cspin] = diff_im;

                        cspin_sum += diff_re * diff_re + diff_im * diff_im;
                    }
                    norm_diff_ptr[col] += cspin_sum;
                }
            }
        } // End of Parallel for reduction
	std::vector<double> norm_diff = norm_diffm.reduce();

        // I would probably need some kind of global reduction here  over the nodes which for now I will ignore.
        MG::GlobalComm::GlobalSum(norm_diff, x);

        return norm_diff;
    }

    /** returns || x ||^2
 * @param x  - CoarseSpinor ref
 * @return   double containing the square norm of the difference
 *
 */
    std::vector<double> Norm2Vec(const CoarseSpinor &x, const CBSubset &subset) {

        const LatticeInfo &x_info = x.GetInfo();

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();
        multivector<double> norm_sqm(ncol);

        // Loop over the sites and sum up the norm
#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    double *norm_sq_ptr = norm_sqm.mine();

                    // Identify the site and the column
                    const float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);

                    // Sum difference over the colorspins
                    double cspin_sum = 0;
#pragma omp simd reduction(+ : cspin_sum)
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {
                        float x_re = x_site_data[RE + n_complex * cspin];
                        float x_im = x_site_data[IM + n_complex * cspin];

                        cspin_sum += x_re * x_re + x_im * x_im;
                    }
                    norm_sq_ptr[col] += cspin_sum;
                }
            }
        } // End of Parallel for reduction
	std::vector<double> norm_sq = norm_sqm.reduce();

        // I would probably need some kind of global reduction here  over the nodes which for now I will ignore.
        MG::GlobalComm::GlobalSum(norm_sq, x);

        return norm_sq;
    }

    /** returns < x[i] | y[i] > = x[i]^H . y[i]
 * @param x  - CoarseSpinor ref
 * @param y  - CoarseSpinor ref
 * @return   double containing the square norm of the difference
 *
 */
    std::vector<std::complex<double>> InnerProductVec(const CoarseSpinor &x, const CoarseSpinor &y,
                                                      const CBSubset &subset) {

        const LatticeInfo &x_info = x.GetInfo();
        const LatticeInfo &y_info = y.GetInfo();
        AssertCompatible(x_info, y_info);

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();

        multivector<std::complex<double>> ipprodm(ncol);

        // Loop over the sites and sum up the norm
#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    std::complex<double> *ipprod_ptr = ipprodm.mine();

                    // Identify the site and the column
                    const float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);
                    const float *y_site_data = y.GetSiteDataPtr(col, cb, cbsite);

                    // Sum difference over the colorspins

                    double cspin_iprod_re = 0;
                    double cspin_iprod_im = 0;
#pragma omp simd reduction(+ : cspin_iprod_re, cspin_iprod_im)
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {

                        cspin_iprod_re += x_site_data[RE + n_complex * cspin] *
                                              y_site_data[RE + n_complex * cspin] +
                                          x_site_data[IM + n_complex * cspin] *
                                              y_site_data[IM + n_complex * cspin];

                        cspin_iprod_im += x_site_data[RE + n_complex * cspin] *
                                              y_site_data[IM + n_complex * cspin] -
                                          x_site_data[IM + n_complex * cspin] *
                                              y_site_data[RE + n_complex * cspin];
                    }
                    ipprod_ptr[col] += std::complex<double>(cspin_iprod_re, cspin_iprod_im);
                }
            }
        } // End of Parallel for reduction
	std::vector<std::complex<double>> ipprod = ipprodm.reduce();

        // Global Reduce
        MG::GlobalComm::GlobalSum(ipprod, x);

        return ipprod;
    }

    /** returns v_ij = < x[i] | y[j] > = x[i]^H . y[j]
 * @param x  - CoarseSpinor ref
 * @param y  - CoarseSpinor ref
 * @return  matrix containing the inner products in column-major
 *
 */
    std::vector<std::complex<double>> InnerProductMat(const CoarseSpinor &x, const CoarseSpinor &y,
                                                      const CBSubset &subset) {

        const LatticeInfo &x_info = x.GetInfo();
        const LatticeInfo &y_info = y.GetInfo();
        AssertCompatible(x_info, y_info);

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType xncol = x.GetNCol();
        IndexType yncol = y.GetNCol();

        multivector<std::complex<float>> ipprodm(xncol * yncol);

        // Loop over the sites and sum up the norm
#pragma omp parallel for collapse(2) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {

                const std::complex<float> *x_site_data =
                    reinterpret_cast<const std::complex<float> *>(x.GetSiteDataPtr(0, cb, cbsite));
                const std::complex<float> *y_site_data =
                    reinterpret_cast<const std::complex<float> *>(y.GetSiteDataPtr(0, cb, cbsite));
                std::complex<float> *ipprod_ptr = ipprodm.mine();

                // ipprod += x_site_data^* * y_site_data
                XGEMM("C", "N", xncol, yncol, num_colorspin, 1.0, x_site_data, num_colorspin,
                      y_site_data, num_colorspin, 1.0, ipprod_ptr, xncol);
            }
        }
	std::vector<std::complex<float>> ipprod = ipprodm.reduce();

        // Global Reduce
        std::vector<std::complex<double>> ipprod_d(ipprod.size());
        for (unsigned int i = 0; i < ipprod.size(); ++i)
            ipprod_d[i] = std::complex<double>(ipprod[i]);
        MG::GlobalComm::GlobalSum(ipprod_d, x);

        return ipprod_d;
    }

    /** returns y[i] = \sum_j x[j] * ip[j,i]
 * @param x  - CoarseSpinor ref
 * @param y  - CoarseSpinor ref
 * @return  CoarseSpinor
 *
 */
    void UpdateVecs(const CoarseSpinor &x, const std::vector<std::complex<double>> &ip,
                    CoarseSpinor &y, const CBSubset &subset) {

        const LatticeInfo &x_info = x.GetInfo();
        const LatticeInfo &y_info = y.GetInfo();
        AssertCompatible(x_info, y_info);

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType xncol = x.GetNCol();
        IndexType yncol = y.GetNCol();
        assert(std::size_t(xncol * yncol) == ip.size());

        std::vector<std::complex<float>> ip_f(ip.size());
        for (unsigned int i = 0; i < ip.size(); ++i) ip_f[i] = std::complex<float>(ip[i]);

        ZeroVec(y);

        // Loop over the sites and sum up the norm
#pragma omp parallel for collapse(2) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {

                // Identify the site and the column
                const std::complex<float> *x_site_data =
                    reinterpret_cast<const std::complex<float> *>(x.GetSiteDataPtr(0, cb, cbsite));
                std::complex<float> *y_site_data =
                    reinterpret_cast<std::complex<float> *>(y.GetSiteDataPtr(0, cb, cbsite));

                // y_site_data = x_site_data * ip
                XGEMM("N", "N", num_colorspin, yncol, xncol, 1.0, x_site_data, num_colorspin,
                      ip_f.data(), xncol, 0.0, y_site_data, num_colorspin);
            }
        } // End of Parallel for reduction
    }

    void ZeroVec(CoarseSpinor &x, const CBSubset &subset) {
        const LatticeInfo &x_info = x.GetInfo();

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);

                    // Set zeros over the colorspins
#pragma omp simd
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {

                        x_site_data[RE + n_complex * cspin] = 0;
                        x_site_data[IM + n_complex * cspin] = 0;
                    }
                }
            }
        } // End of Parallel for region
    }

    void CopyVec(CoarseSpinor &x, const CoarseSpinor &y, const CBSubset &subset) {
        CopyVec(x, 0, x.GetNCol(), y, 0, subset);
    }

    void CopyVec(CoarseSpinor &x, int xcol0, int xcol1, const CoarseSpinor &y, int ycol0,
                 const CBSubset &subset) {

        const LatticeInfo &x_info = x.GetInfo();
        const LatticeInfo &y_info = y.GetInfo();
        AssertCompatible(x_info, y_info);

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType xncol = x.GetNCol();
        assert(xcol1 <= xncol);
        IndexType ncol = std::max(0, xcol1 - xcol0);
        assert(ycol0 + ncol <= y.GetNCol());

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    float *x_site_data = x.GetSiteDataPtr(xcol0 + col, cb, cbsite);
                    const float *y_site_data = y.GetSiteDataPtr(ycol0 + col, cb, cbsite);

                    // Do copies over the colorspins
#pragma omp simd
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {

                        x_site_data[RE + n_complex * cspin] = y_site_data[RE + n_complex * cspin];
                        x_site_data[IM + n_complex * cspin] = y_site_data[IM + n_complex * cspin];
                    }
                }
            }
        } // End of Parallel for region
    }

    void ScaleVec(const std::vector<float> &alpha, CoarseSpinor &x, const CBSubset &subset) {

        assert(alpha.size() == (size_t)x.GetNCol());
        const LatticeInfo &x_info = x.GetInfo();

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);
                    float alpha_col = alpha[col];

#pragma omp simd
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {

                        x_site_data[RE + n_complex * cspin] *= alpha_col;
                        x_site_data[IM + n_complex * cspin] *= alpha_col;
                    }
                }
            }
        } // End of Parallel for region
    }

    void ScaleVec(const std::vector<std::complex<float>> &alpha, CoarseSpinor &x,
                  const CBSubset &subset) {

        const LatticeInfo &x_info = x.GetInfo();

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);
                    std::complex<float> alpha_col = alpha[col];

#pragma omp simd
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {
                        std::complex<float> t(x_site_data[RE + n_complex * cspin],
                                              x_site_data[IM + n_complex * cspin]);

                        t *= alpha_col;
                        x_site_data[RE + n_complex * cspin] = std::real(t);
                        x_site_data[IM + n_complex * cspin] = std::imag(t);
                    }
                }
            }
        } // End of Parallel for reduction
    }

    namespace {
        template <typename T> inline float toFloat(const T &f);
        template <typename T> inline float toFloat(const T &f) { return float(f); }
        template <typename T> inline std::complex<float> toFloat(const std::complex<T> &f) {
            return std::complex<float>(f);
        }
    }

    template <typename T>
    void AxpyVecT(const std::vector<T> &alpha, const CoarseSpinor &x, CoarseSpinor &y,
                  const CBSubset &subset) {
        const LatticeInfo &x_info = x.GetInfo();
        const LatticeInfo &y_info = y.GetInfo();
        AssertCompatible(x_info, y_info);

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    const std::complex<float> *x_site_data =
                        reinterpret_cast<const std::complex<float> *>(
                            x.GetSiteDataPtr(col, cb, cbsite));
                    std::complex<float> *y_site_data =
                        reinterpret_cast<std::complex<float> *>(y.GetSiteDataPtr(col, cb, cbsite));
                    auto alpha_col = toFloat(alpha[col]);

                    // Do axpy over the colorspins
#pragma omp simd
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {
                        y_site_data[cspin] += x_site_data[cspin] * alpha_col;
                    }
                }
            }
        } // End of Parallel for region
    }

    void AxpyVec(const std::vector<std::complex<float>> &alpha, const CoarseSpinor &x,
                 CoarseSpinor &y, const CBSubset &subset) {
        AxpyVecT(alpha, x, y, subset);
    }

    void AxpyVec(const std::vector<float> &alpha, const CoarseSpinor &x, CoarseSpinor &y,
                 const CBSubset &subset) {
        AxpyVecT(alpha, x, y, subset);
    }

    void AxpyVec(const std::vector<std::complex<double>> &alpha, const CoarseSpinor &x,
                 CoarseSpinor &y, const CBSubset &subset) {
        AxpyVecT(alpha, x, y, subset);
    }

    void AxpyVec(const std::vector<double> &alpha, const CoarseSpinor &x, CoarseSpinor &y,
                 const CBSubset &subset) {
        AxpyVecT(alpha, x, y, subset);
    }
    void YpeqxVec(const CoarseSpinor &x, CoarseSpinor &y, const CBSubset &subset) {
        const LatticeInfo &x_info = x.GetInfo();
        const LatticeInfo &y_info = y.GetInfo();
        AssertCompatible(x_info, y_info);

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    const float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);
                    float *y_site_data = y.GetSiteDataPtr(col, cb, cbsite);

                    // Do over the colorspins
#pragma omp simd
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {

                        y_site_data[RE + n_complex * cspin] += x_site_data[RE + n_complex * cspin];
                        y_site_data[IM + n_complex * cspin] += x_site_data[IM + n_complex * cspin];
                    }
                }
            }
        } // End of Parallel for region
    }

    void YmeqxVec(const CoarseSpinor &x, CoarseSpinor &y, const CBSubset &subset) {
        const LatticeInfo &x_info = x.GetInfo();
        const LatticeInfo &y_info = y.GetInfo();
        AssertCompatible(x_info, y_info);

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    const float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);
                    float *y_site_data = y.GetSiteDataPtr(col, cb, cbsite);

                    // Do over the colorspins
#pragma omp simd
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {

                        y_site_data[RE + n_complex * cspin] -= x_site_data[RE + n_complex * cspin];
                        y_site_data[IM + n_complex * cspin] -= x_site_data[IM + n_complex * cspin];
                    }
                }
            }
        } // End of Parallel for region
    }

    void BiCGStabPUpdate(const std::vector<std::complex<float>> &beta, const CoarseSpinor &r,
                         const std::vector<std::complex<float>> &omega, const CoarseSpinor &v,
                         CoarseSpinor &p, const CBSubset &subset) {
        const LatticeInfo &r_info = r.GetInfo();
        const LatticeInfo &p_info = p.GetInfo();
        const LatticeInfo &v_info = v.GetInfo();

        AssertCompatible(r_info, p_info);
        AssertCompatible(v_info, r_info);

        IndexType num_cbsites = p_info.GetNumCBSites();
        IndexType num_colorspin = p.GetNumColorSpin();
        IndexType ncol = r.GetNCol();

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    const float *r_site_data = r.GetSiteDataPtr(col, cb, cbsite);
                    const float *v_site_data = v.GetSiteDataPtr(col, cb, cbsite);
                    float *p_site_data = p.GetSiteDataPtr(col, cb, cbsite);
                    std::complex<float> beta_col = beta[col];
                    std::complex<float> omega_col = omega[col];

                    // Sum difference over the colorspins
#pragma omp simd
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {

                        const std::complex<float> c_p(p_site_data[RE + n_complex * cspin],
                                                      p_site_data[IM + n_complex * cspin]);

                        const std::complex<float> c_r(r_site_data[RE + n_complex * cspin],
                                                      r_site_data[IM + n_complex * cspin]);

                        const std::complex<float> c_v(v_site_data[RE + n_complex * cspin],
                                                      v_site_data[IM + n_complex * cspin]);

                        std::complex<float> res = c_r + beta_col * (c_p - omega_col * c_v);

                        p_site_data[RE + n_complex * cspin] = res.real();
                        p_site_data[IM + n_complex * cspin] = res.imag();
                    }
                }
            }
        } // End of Parallel for region
    }

    void BiCGStabXUpdate(const std::vector<std::complex<float>> &omega, const CoarseSpinor &r,
                         const std::vector<std::complex<float>> &alpha, const CoarseSpinor &p,
                         CoarseSpinor &x, const CBSubset &subset) {
        const LatticeInfo &r_info = r.GetInfo();
        const LatticeInfo &p_info = p.GetInfo();
        const LatticeInfo &x_info = x.GetInfo();

        AssertCompatible(r_info, p_info);
        AssertCompatible(x_info, r_info);

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    const float *r_site_data = r.GetSiteDataPtr(col, cb, cbsite);
                    const float *p_site_data = p.GetSiteDataPtr(col, cb, cbsite);
                    float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);
                    std::complex<float> alpha_col = alpha[col];
                    std::complex<float> omega_col = omega[col];

                    // Do over the colorspins
#pragma omp simd
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {

                        const std::complex<float> c_p(p_site_data[RE + n_complex * cspin],
                                                      p_site_data[IM + n_complex * cspin]);

                        const std::complex<float> c_r(r_site_data[RE + n_complex * cspin],
                                                      r_site_data[IM + n_complex * cspin]);

                        const std::complex<float> res = omega_col * c_r + alpha_col * c_p;

                        x_site_data[RE + n_complex * cspin] += res.real();
                        x_site_data[IM + n_complex * cspin] += res.imag();
                    }
                }
            }
        } // End of Parallel for region
    }

    void XmyzVec(const CoarseSpinor &x, const CoarseSpinor &y, CoarseSpinor &z,
                 const CBSubset &subset) {
        const LatticeInfo &x_info = x.GetInfo();
        const LatticeInfo &y_info = y.GetInfo();
        const LatticeInfo &z_info = z.GetInfo();

        AssertCompatible(x_info, y_info);
        AssertCompatible(z_info, x_info);

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    const float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);
                    const float *y_site_data = y.GetSiteDataPtr(col, cb, cbsite);
                    float *z_site_data = z.GetSiteDataPtr(col, cb, cbsite);

                    // Do over the colorspins
#pragma omp simd
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {

                        z_site_data[RE + n_complex * cspin] = x_site_data[RE + n_complex * cspin] -
                                                              y_site_data[RE + n_complex * cspin];
                        z_site_data[IM + n_complex * cspin] = x_site_data[IM + n_complex * cspin] -
                                                              y_site_data[IM + n_complex * cspin];
                    }
                }
            }
        } // End of Parallel for region
    }

    void GetColumns(const CoarseSpinor &x, const CBSubset &subset, float *y, size_t ld) {
        const LatticeInfo &x_info = x.GetInfo();

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();
        int cb0 = subset.start;

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    const float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);

                    // Do over the colorspins
#pragma omp simd
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {

                        y[RE + n_complex * cspin +
                          ((cb - cb0) * num_cbsites + cbsite) * num_colorspin * n_complex +
                          ld * col] = x_site_data[RE + n_complex * cspin];
                        y[IM + n_complex * cspin +
                          ((cb - cb0) * num_cbsites + cbsite) * num_colorspin * n_complex +
                          ld * col] = x_site_data[IM + n_complex * cspin];
                    }
                }
            }
        } // End of Parallel for region
    }

    void PutColumns(const float *y, size_t ld, CoarseSpinor &x, const CBSubset &subset) {
        const LatticeInfo &x_info = x.GetInfo();

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();
        assert(ld >= (unsigned int)(n_complex * num_colorspin * num_cbsites *
                                    (subset.end - subset.start)));
        int cb0 = subset.start;

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);

                    // Do over the colorspins
#pragma omp simd
                    for (int cspin = 0; cspin < num_colorspin; ++cspin) {

                        x_site_data[RE + n_complex * cspin] =
                            y[RE + n_complex * cspin +
                              ((cb - cb0) * num_cbsites + cbsite) * num_colorspin * n_complex +
                              ld * col];
                        x_site_data[IM + n_complex * cspin] =
                            y[IM + n_complex * cspin +
                              ((cb - cb0) * num_cbsites + cbsite) * num_colorspin * n_complex +
                              ld * col];
                    }
                }
            }
        } // End of Parallel for region
    }

    void Gamma5Vec(CoarseSpinor &x, const CBSubset &subset) {
        const LatticeInfo &x_info = x.GetInfo();

        IndexType num_cbsites = x_info.GetNumCBSites();
        IndexType num_colorspin = x.GetNumColorSpin();
        IndexType ncol = x.GetNCol();

#pragma omp parallel for collapse(3) schedule(static)
        for (int cb = subset.start; cb < subset.end; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                for (int col = 0; col < ncol; ++col) {

                    // Identify the site and the column
                    float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);

                    // Do over the colorspins
#pragma omp simd
                    for (int cspin = num_colorspin / 2; cspin < num_colorspin; ++cspin) {

                        x_site_data[RE + n_complex * cspin] *= -1;
                        x_site_data[IM + n_complex * cspin] *= -1;
                    }
                }
            }
        } // End of Parallel for region
    }

    /**** NOT 100% sure how to test this easily ******/
    void Gaussian(CoarseSpinor &x, const CBSubset &subset) {
        const LatticeInfo &info = x.GetInfo();
        const IndexType num_colorspin = info.GetNumColors() * info.GetNumSpins();
        const IndexType num_cbsites = info.GetNumCBSites();
        const IndexType ncol = x.GetNCol();

        /* FIXME: This is quick and dirty and nonreproducible if the lattice is distributed
	 *  among the processes is a different way. 
	 */

#ifdef MG_QMP_COMMS
        static const int node = QMP_get_node_number();
#else
        static const int node = 0;
#endif
        static std::mt19937_64 twister_engine(10 + node);

        // Create the thread team
#pragma omp single
        {
            // A normal distribution centred on 0, with width 1
            std::normal_distribution<> normal_dist(0.0, 1.0);

            for (int cb = subset.start; cb < subset.end; ++cb) {
                for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                    for (int col = 0; col < ncol; ++col) {

                        float *x_site_data = x.GetSiteDataPtr(col, cb, cbsite);

                        for (int cspin = 0; cspin < num_colorspin; ++cspin) {

                            // Using the Twister Engine, draw from the normal distribution
                            x_site_data[RE + n_complex * cspin] = normal_dist(twister_engine);
                            x_site_data[IM + n_complex * cspin] = normal_dist(twister_engine);
                        }
                    }
                }
            }
        }
    }

    void ZeroGauge(CoarseGauge &gauge) {
        const LatticeInfo &info = gauge.GetInfo();
        const int num_cbsites = info.GetNumCBSites();

        // diag_data
#pragma omp parallel for collapse(2)
        for (int cb = 0; cb < n_checkerboard; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                float *data = gauge.GetSiteDiagDataPtr(cb, cbsite);

#pragma omp simd aligned(data : 64)
                for (int j = 0; j < gauge.GetLinkOffset(); ++j) { data[j] = 0; }
            }
        }

        // offdiag_data
#pragma omp parallel for collapse(2)
        for (int cb = 0; cb < n_checkerboard; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                float *data = gauge.GetSiteDirDataPtr(cb, cbsite, 0);
#pragma omp simd aligned(data : 64)
                for (int j = 0; j < gauge.GetSiteOffset(); ++j) { data[j] = 0; }
            }
        }

        // inv diag_data
#pragma omp parallel for collapse(2)
        for (int cb = 0; cb < n_checkerboard; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                float *data = gauge.GetSiteInvDiagDataPtr(cb, cbsite);

#pragma omp simd aligned(data : 64)
                for (int j = 0; j < gauge.GetLinkOffset(); ++j) { data[j] = 0; }
            }
        }

        // AD_data
#pragma omp parallel for collapse(2)
        for (int cb = 0; cb < n_checkerboard; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                float *data = gauge.GetSiteDirADDataPtr(cb, cbsite, 0);
#pragma omp simd aligned(data : 64)
                for (int j = 0; j < gauge.GetSiteOffset(); ++j) { data[j] = 0; }
            }
        }

        // DA_data
#pragma omp parallel for collapse(2)
        for (int cb = 0; cb < n_checkerboard; ++cb) {
            for (int cbsite = 0; cbsite < num_cbsites; ++cbsite) {
                float *data = gauge.GetSiteDirDADataPtr(cb, cbsite, 0);
#pragma omp simd aligned(data : 64)
                for (int j = 0; j < gauge.GetSiteOffset(); ++j) { data[j] = 0; }
            }
        }
    }
}
