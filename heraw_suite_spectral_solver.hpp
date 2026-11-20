// heraw_suite_spectral_solver.hpp -- Stage 2 Information-Theoretic Solvers:
//   - Q1 / R7: Native Joint Solver + Arrow-Head Double Schur Elimination + Joint Covariance
//   - Q2 / R5 / Cross-Section #2: Bayer 4-Mode & X-Trans 6/10-Col Coset Basis + Data Rank + L_{Y<-C}
//   - Q3: Convex B-Spline PSD Tile Spectral Map K(x)
//   - Q4 / Q5 / R8: Stable Upper-Tail Probit + TicoRAW Lifting Exact Adjoint + Footprint Router
//   - Q6 / R9: Three-Tier 1x True-RGB Basis (native | bandlimited | sr2x) + SPD Sigma_H + DoF nu_r
// Pure 7-bit ASCII C++20 header. Zero external dependencies.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace heraw_spec {

// ---------------------------------------------------------------------------
// Dense Small-Matrix Linear Algebra (SPD Solve, Rank-Revealing QR, SymEigen)
// ---------------------------------------------------------------------------
inline bool solve_spd_cholesky(std::vector<double>& A, std::vector<double>& b, int n, double ridge = 1e-12) {
    if (n <= 0 || static_cast<int>(A.size()) < n * n || static_cast<int>(b.size()) < n) return false;
    std::vector<double> L(static_cast<size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j <= i; ++j) {
            double s = A[static_cast<size_t>(i) * n + j];
            if (i == j) s += ridge;
            for (int k = 0; k < j; ++k) s -= L[static_cast<size_t>(i) * n + k] * L[static_cast<size_t>(j) * n + k];
            if (i == j) {
                if (!(s > 0.0) || !std::isfinite(s)) return false;
                L[static_cast<size_t>(i) * n + j] = std::sqrt(s);
            } else {
                L[static_cast<size_t>(i) * n + j] = s / L[static_cast<size_t>(j) * n + j];
            }
        }
    }
    std::vector<double> y(static_cast<size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        double s = b[static_cast<size_t>(i)];
        for (int k = 0; k < i; ++k) s -= L[static_cast<size_t>(i) * n + k] * y[static_cast<size_t>(k)];
        y[static_cast<size_t>(i)] = s / L[static_cast<size_t>(i) * n + i];
    }
    for (int i = n - 1; i >= 0; --i) {
        double s = y[static_cast<size_t>(i)];
        for (int k = i + 1; k < n; ++k) s -= L[static_cast<size_t>(k) * n + i] * b[static_cast<size_t>(k)];
        b[static_cast<size_t>(i)] = s / L[static_cast<size_t>(i) * n + i];
    }
    return true;
}

// Symmetric Jacobi eigendecomposition A = V diag(evals) V^T (returns ascending evals).
inline void sym_jacobi_eigen(std::vector<double> A, int n, std::vector<double>& evals, std::vector<double>& V) {
    evals.assign(static_cast<size_t>(n), 0.0);
    V.assign(static_cast<size_t>(n) * n, 0.0);
    if (n <= 0) return;
    for (int i = 0; i < n; ++i) V[static_cast<size_t>(i) * n + i] = 1.0;
    const int max_sweeps = 64 * n * std::max(1, n);
    for (int sw = 0; sw < max_sweeps; ++sw) {
        int p = 0, q = 0;
        double mx = 0.0;
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                double av = std::abs(A[static_cast<size_t>(i) * n + j]);
                if (av > mx) { mx = av; p = i; q = j; }
            }
        }
        if (mx <= 1e-13) break;
        const double app = A[static_cast<size_t>(p) * n + p];
        const double aqq = A[static_cast<size_t>(q) * n + q];
        const double apq = A[static_cast<size_t>(p) * n + q];
        const double tau = (aqq - app) / (2.0 * apq);
        const double t = (tau >= 0.0) ? (1.0 / (tau + std::sqrt(1.0 + tau * tau)))
                                      : (-1.0 / (-tau + std::sqrt(1.0 + tau * tau)));
        const double c = 1.0 / std::sqrt(1.0 + t * t);
        const double s = t * c;
        for (int r = 0; r < n; ++r) {
            if (r == p || r == q) continue;
            const double arp = A[static_cast<size_t>(r) * n + p];
            const double arq = A[static_cast<size_t>(r) * n + q];
            const double nrp = c * arp - s * arq;
            const double nrq = s * arp + c * arq;
            A[static_cast<size_t>(r) * n + p] = A[static_cast<size_t>(p) * n + r] = nrp;
            A[static_cast<size_t>(r) * n + q] = A[static_cast<size_t>(q) * n + r] = nrq;
        }
        A[static_cast<size_t>(p) * n + p] = app - t * apq;
        A[static_cast<size_t>(q) * n + q] = aqq + t * apq;
        A[static_cast<size_t>(p) * n + q] = A[static_cast<size_t>(q) * n + p] = 0.0;
        for (int r = 0; r < n; ++r) {
            const double vrp = V[static_cast<size_t>(r) * n + p];
            const double vrq = V[static_cast<size_t>(r) * n + q];
            V[static_cast<size_t>(r) * n + p] = c * vrp - s * vrq;
            V[static_cast<size_t>(r) * n + q] = s * vrp + c * vrq;
        }
    }
    for (int i = 0; i < n; ++i) evals[static_cast<size_t>(i)] = A[static_cast<size_t>(i) * n + i];
    // Sort eigenvalues ascending
    for (int i = 0; i < n; ++i) {
        int best = i;
        for (int j = i + 1; j < n; ++j) {
            if (evals[static_cast<size_t>(j)] < evals[static_cast<size_t>(best)]) best = j;
        }
        if (best != i) {
            std::swap(evals[static_cast<size_t>(i)], evals[static_cast<size_t>(best)]);
            for (int r = 0; r < n; ++r) {
                std::swap(V[static_cast<size_t>(r) * n + i], V[static_cast<size_t>(r) * n + best]);
            }
        }
    }
}

// Moore-Penrose symmetric pseudoinverse A^+ of n x n matrix via Jacobi eigendecomposition.
inline std::vector<double> sym_pinv(const std::vector<double>& A, int n, double rtol = 1e-10, int* out_rank = nullptr) {
    std::vector<double> evals, V;
    sym_jacobi_eigen(A, n, evals, V);
    double max_ev = 0.0;
    for (double v : evals) max_ev = std::max(max_ev, std::abs(v));
    const double thr = std::max(1e-15, rtol * max_ev);
    std::vector<double> P(static_cast<size_t>(n) * n, 0.0);
    int rnk = 0;
    for (int k = 0; k < n; ++k) {
        if (evals[static_cast<size_t>(k)] > thr) {
            ++rnk;
            const double inv_e = 1.0 / evals[static_cast<size_t>(k)];
            for (int i = 0; i < n; ++i) {
                const double vi = V[static_cast<size_t>(i) * n + k];
                for (int j = 0; j < n; ++j) {
                    P[static_cast<size_t>(i) * n + j] += inv_e * vi * V[static_cast<size_t>(j) * n + k];
                }
            }
        }
    }
    if (out_rank) *out_rank = rnk;
    return P;
}

// Solve M * X = B for symmetric PSD M (n x n) and right-hand side B (n x m) via pseudoinverse.
inline std::vector<double> sym_pinv_mul(const std::vector<double>& M, int n,
                                        const std::vector<double>& B, int m, double rtol = 1e-10) {
    auto Mp = sym_pinv(M, n, rtol);
    std::vector<double> out(static_cast<size_t>(n) * m, 0.0);
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < n; ++k) {
            const double mik = Mp[static_cast<size_t>(i) * n + k];
            for (int j = 0; j < m; ++j) {
                out[static_cast<size_t>(i) * m + j] += mik * B[static_cast<size_t>(k) * m + j];
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Q1 & R7: Arrow-Head Double Schur Elimination + Native Joint Tile Solver
// ---------------------------------------------------------------------------
// Model per frame k:
//   y_k = B_k u + J_k xi_k + U_k a + eps_k,   cov(eps_k) = W_k^{-1}
// Local nuisance xi_k ~ (0, Lambda_k^{-1}), shared anchor error a ~ (0, Lambda_a^{-1}).
struct ArrowHeadSchurSolver {
    int du = 0;
    int da = 0;
    std::vector<double> H; // du x du
    std::vector<double> L; // du x da
    std::vector<double> G; // da x da (initialized with Lambda_a)
    std::vector<double> h; // du
    std::vector<double> g; // da
    double c_scalar = 0.0;

    void reset(int dim_u, int dim_a, const std::vector<double>* lambda_a_diag = nullptr) {
        du = dim_u;
        da = dim_a;
        H.assign(static_cast<size_t>(du) * du, 0.0);
        L.assign(static_cast<size_t>(du) * da, 0.0);
        G.assign(static_cast<size_t>(da) * da, 0.0);
        for (int i = 0; i < da; ++i) {
            G[static_cast<size_t>(i) * da + i] =
                (lambda_a_diag && i < static_cast<int>(lambda_a_diag->size())) ? (*lambda_a_diag)[static_cast<size_t>(i)] : 0.0;
        }
        h.assign(static_cast<size_t>(du), 0.0);
        g.assign(static_cast<size_t>(da), 0.0);
        c_scalar = 0.0;
    }

    // Add frame observations (m rows): B (m x du), U (m x da), J (m x dj), w (m diagonal weights), y (m).
    // Performs inner Schur elimination Q_k = W_k - W_k J_k (J_k^T W_k J_k + Lambda_k)^+ J_k^T W_k in O(1) wrt N.
    void add_frame(
        int m,
        const double* B, // m * du
        const double* U, // m * da (nullable if da == 0)
        int dj,
        const double* J, // m * dj (nullable if dj == 0)
        const double* lambda_j_diag, // length dj (nullable -> 0)
        const double* w_diag, // length m
        const double* y       // length m
    ) {
        if (m <= 0) return;
        // Build Q_k = W_k - W_k J (J^T W_k J + Lambda_j)^+ J^T W_k implicitly via effective m x p projections.
        // We compute Z = Q_k * [B, U, y] = W [B, U, y] - W J (J^T W J + L)^+ (J^T W [B, U, y]).
        const int ncols = du + da + 1;
        std::vector<double> Wcols(static_cast<size_t>(m) * ncols, 0.0);
        std::vector<double> WB(static_cast<size_t>(m) * ncols, 0.0);
        for (int r = 0; r < m; ++r) {
            const double w = w_diag ? std::max(0.0, w_diag[r]) : 1.0;
            for (int j = 0; j < du; ++j) {
                Wcols[static_cast<size_t>(r) * ncols + j] = B[static_cast<size_t>(r) * du + j];
                WB[static_cast<size_t>(r) * ncols + j] = w * B[static_cast<size_t>(r) * du + j];
            }
            for (int j = 0; j < da; ++j) {
                const double uv = U ? U[static_cast<size_t>(r) * da + j] : 0.0;
                Wcols[static_cast<size_t>(r) * ncols + du + j] = uv;
                WB[static_cast<size_t>(r) * ncols + du + j] = w * uv;
            }
            const double yv = y ? y[r] : 0.0;
            Wcols[static_cast<size_t>(r) * ncols + du + da] = yv;
            WB[static_cast<size_t>(r) * ncols + du + da] = w * yv;
        }
        std::vector<double> Qcols = WB;
        if (dj > 0 && J) {
            std::vector<double> JwJ(static_cast<size_t>(dj) * dj, 0.0);
            std::vector<double> JwX(static_cast<size_t>(dj) * ncols, 0.0);
            for (int r = 0; r < m; ++r) {
                const double w = w_diag ? std::max(0.0, w_diag[r]) : 1.0;
                for (int a = 0; a < dj; ++a) {
                    const double ja = J[static_cast<size_t>(r) * dj + a] * w;
                    for (int b = 0; b < dj; ++b) JwJ[static_cast<size_t>(a) * dj + b] += ja * J[static_cast<size_t>(r) * dj + b];
                    for (int c = 0; c < ncols; ++c) JwX[static_cast<size_t>(a) * ncols + c] += ja * Wcols[static_cast<size_t>(r) * ncols + c];
                }
            }
            if (lambda_j_diag) {
                for (int a = 0; a < dj; ++a) JwJ[static_cast<size_t>(a) * dj + a] += lambda_j_diag[a];
            }
            auto sol = sym_pinv_mul(JwJ, dj, JwX, ncols);
            for (int r = 0; r < m; ++r) {
                const double w = w_diag ? std::max(0.0, w_diag[r]) : 1.0;
                for (int c = 0; c < ncols; ++c) {
                    double jval = 0.0;
                    for (int a = 0; a < dj; ++a) jval += J[static_cast<size_t>(r) * dj + a] * sol[static_cast<size_t>(a) * ncols + c];
                    Qcols[static_cast<size_t>(r) * ncols + c] -= w * jval;
                }
            }
        }
        // Accumulate B^T Q B, B^T Q U, U^T Q U, B^T Q y, U^T Q y, y^T Q y
        for (int r = 0; r < m; ++r) {
            for (int i = 0; i < du; ++i) {
                const double bi = Wcols[static_cast<size_t>(r) * ncols + i];
                for (int j = 0; j < du; ++j) H[static_cast<size_t>(i) * du + j] += bi * Qcols[static_cast<size_t>(r) * ncols + j];
                for (int j = 0; j < da; ++j) L[static_cast<size_t>(i) * da + j] += bi * Qcols[static_cast<size_t>(r) * ncols + du + j];
                h[static_cast<size_t>(i)] += bi * Qcols[static_cast<size_t>(r) * ncols + du + da];
            }
            for (int i = 0; i < da; ++i) {
                const double ui = Wcols[static_cast<size_t>(r) * ncols + du + i];
                for (int j = 0; j < da; ++j) G[static_cast<size_t>(i) * da + j] += ui * Qcols[static_cast<size_t>(r) * ncols + du + j];
                g[static_cast<size_t>(i)] += ui * Qcols[static_cast<size_t>(r) * ncols + du + da];
            }
            c_scalar += Wcols[static_cast<size_t>(r) * ncols + du + da] * Qcols[static_cast<size_t>(r) * ncols + du + da];
        }
    }

    struct FinalReduced {
        std::vector<double> F_eff; // du x du
        std::vector<double> h_eff; // du
        double c_eff = 0.0;        // R7 constant term: c - g^T G^+ g
    };

    FinalReduced finalize() const {
        FinalReduced res;
        res.F_eff = H;
        res.h_eff = h;
        res.c_eff = c_scalar;
        if (da <= 0) return res;
        // Compute G^+ L^T (da x du) and G^+ g (da x 1)
        std::vector<double> Lt(static_cast<size_t>(da) * du, 0.0);
        for (int i = 0; i < du; ++i)
            for (int j = 0; j < da; ++j)
                Lt[static_cast<size_t>(j) * du + i] = L[static_cast<size_t>(i) * da + j];
        auto GpLt = sym_pinv_mul(G, da, Lt, du);
        auto Gpg = sym_pinv_mul(G, da, g, 1);
        for (int i = 0; i < du; ++i) {
            for (int j = 0; j < du; ++j) {
                double sub = 0.0;
                for (int k = 0; k < da; ++k) sub += L[static_cast<size_t>(i) * da + k] * GpLt[static_cast<size_t>(k) * du + j];
                res.F_eff[static_cast<size_t>(i) * du + j] -= sub;
            }
            double sub_h = 0.0;
            for (int k = 0; k < da; ++k) sub_h += L[static_cast<size_t>(i) * da + k] * Gpg[static_cast<size_t>(k)];
            res.h_eff[static_cast<size_t>(i)] -= sub_h;
        }
        for (int k = 0; k < da; ++k) res.c_eff -= g[static_cast<size_t>(k)] * Gpg[static_cast<size_t>(k)];
        return res;
    }
};

// Streaming-safe Native Joint Solver (R7: solves directly against y_k = A_k X + eps_k; frame 0 only defines coordinates/photometric scale).
struct NativeJointTileSolver {
    int du = 0;
    std::vector<double> F;
    std::vector<double> h;
    double c_scalar = 0.0;

    void reset(int dim_u) {
        du = dim_u;
        F.assign(static_cast<size_t>(du) * du, 0.0);
        h.assign(static_cast<size_t>(du), 0.0);
        c_scalar = 0.0;
    }

    void add_observation(int m, const double* B_k, const double* w_diag, const double* y_k) {
        if (m <= 0 || du <= 0) return;
        for (int r = 0; r < m; ++r) {
            const double w = w_diag ? std::max(0.0, w_diag[r]) : 1.0;
            const double yv = y_k[r];
            for (int i = 0; i < du; ++i) {
                const double wbi = w * B_k[static_cast<size_t>(r) * du + i];
                for (int j = 0; j < du; ++j) {
                    F[static_cast<size_t>(i) * du + j] += wbi * B_k[static_cast<size_t>(r) * du + j];
                }
                h[static_cast<size_t>(i)] += wbi * yv;
            }
            c_scalar += w * yv * yv;
        }
    }

    std::vector<double> solve_u(const std::vector<double>* lambda_prior = nullptr) const {
        if (du <= 0) return {};
        std::vector<double> A = F;
        std::vector<double> b = h;
        if (lambda_prior && static_cast<int>(lambda_prior->size()) == du * du) {
            for (int i = 0; i < du * du; ++i) A[static_cast<size_t>(i)] += (*lambda_prior)[static_cast<size_t>(i)];
        }
        return sym_pinv_mul(A, du, b, 1);
    }
};

// ---------------------------------------------------------------------------
// Q2 & R5: Nuisance-Projected Data Rank, Leakage Operator L_{Y<-C}, and Prior Variance Reduction s_l
// ---------------------------------------------------------------------------
struct ChromaticRankReport {
    std::vector<double> F_data;        // dy x dy: B_Y^* (I - P_D) B_Y
    std::vector<double> F_cond;        // dy x dy: B_Y^* Q_Lambda B_Y
    std::vector<double> L_y_from_c;    // dy x dm: leakage from dropped chroma columns D_miss
    double lambda_min_data = 0.0;
    double lambda_min_cond = 0.0;
    double leakage_op_norm = 0.0;
    double prior_var_reduction_s = 0.0; // renamed from Q2 s_l per Audit Cross-Section #2
    int effective_data_rank = 0;
};

// Evaluate pure luminance support AFTER nuisance projection onto D (e.g. D = [B_{C,low}, B_{C,high}, J_xi, U_a])
// plus leakage from dropped chroma columns D_miss and conditional variance reduction s_l along contrast vector l_vec.
inline ChromaticRankReport analyze_luma_vs_chroma_rank(
    int m_obs,
    int dy,
    const double* B_Y_w,     // m_obs x dy (already pre-whitened by W^{1/2})
    int dv,
    const double* D_w,       // m_obs x dv (already pre-whitened by W^{1/2})
    const std::vector<double>& lambda_v_diag, // length dv
    int dm,
    const double* D_miss_w,  // m_obs x dm (already pre-whitened, nullable if dm==0)
    const std::vector<double>& lambda_y_diag, // length dy
    const std::vector<double>& contrast_l     // length dy
) {
    ChromaticRankReport rep;
    if (m_obs <= 0 || dy <= 0 || !B_Y_w) return rep;

    auto mul_AtB = [&](int r, int cA, const double* A, int cB, const double* B) {
        std::vector<double> out(static_cast<size_t>(cA) * cB, 0.0);
        for (int k = 0; k < r; ++k) {
            for (int i = 0; i < cA; ++i) {
                const double aki = A[static_cast<size_t>(k) * cA + i];
                for (int j = 0; j < cB; ++j) {
                    out[static_cast<size_t>(i) * cB + j] += aki * B[static_cast<size_t>(k) * cB + j];
                }
            }
        }
        return out;
    };

    const auto BtB = mul_AtB(m_obs, dy, B_Y_w, dy, B_Y_w);
    rep.F_data = BtB;
    rep.F_cond = BtB;

    std::vector<double> BtD, DtD, Dp;
    if (dv > 0 && D_w) {
        DtD = mul_AtB(m_obs, dv, D_w, dv, D_w);
        BtD = mul_AtB(m_obs, dy, B_Y_w, dv, D_w);

        // Pure data projection (I - P_D) => F_data = B^T B - B^T D (D^T D)^+ D^T B
        std::vector<double> DtB(static_cast<size_t>(dv) * dy, 0.0);
        for (int i = 0; i < dy; ++i)
            for (int j = 0; j < dv; ++j)
                DtB[static_cast<size_t>(j) * dy + i] = BtD[static_cast<size_t>(i) * dv + j];
        auto DtDp_DtB = sym_pinv_mul(DtD, dv, DtB, dy);
        for (int i = 0; i < dy; ++i) {
            for (int j = 0; j < dy; ++j) {
                double sub = 0.0;
                for (int k = 0; k < dv; ++k) sub += BtD[static_cast<size_t>(i) * dv + k] * DtDp_DtB[static_cast<size_t>(k) * dy + j];
                rep.F_data[static_cast<size_t>(i) * dy + j] -= sub;
            }
        }

        // Prior-conditioned Q_Lambda = I - D (D^T D + Lambda_v)^+ D^T
        std::vector<double> DtD_L = DtD;
        for (int i = 0; i < dv; ++i) {
            const double lv = (i < static_cast<int>(lambda_v_diag.size())) ? lambda_v_diag[static_cast<size_t>(i)] : 0.0;
            DtD_L[static_cast<size_t>(i) * dv + i] += lv;
        }
        auto DtDLp_DtB = sym_pinv_mul(DtD_L, dv, DtB, dy);
        for (int i = 0; i < dy; ++i) {
            for (int j = 0; j < dy; ++j) {
                double sub = 0.0;
                for (int k = 0; k < dv; ++k) sub += BtD[static_cast<size_t>(i) * dv + k] * DtDLp_DtB[static_cast<size_t>(k) * dy + j];
                rep.F_cond[static_cast<size_t>(i) * dy + j] -= sub;
            }
        }

        // Leakage operator L_{Y <- C} = (F_cond + Lambda_Y)^+ * B_Y^T Q_Lambda D_miss (R5)
        if (dm > 0 && D_miss_w) {
            auto BtDm = mul_AtB(m_obs, dy, B_Y_w, dm, D_miss_w);
            auto DtDm = mul_AtB(m_obs, dv, D_w, dm, D_miss_w);
            auto DtDLp_DtDm = sym_pinv_mul(DtD_L, dv, DtDm, dm);
            std::vector<double> rhs_miss = BtDm;
            for (int i = 0; i < dy; ++i) {
                for (int j = 0; j < dm; ++j) {
                    double sub = 0.0;
                    for (int k = 0; k < dv; ++k) sub += BtD[static_cast<size_t>(i) * dv + k] * DtDLp_DtDm[static_cast<size_t>(k) * dm + j];
                    rhs_miss[static_cast<size_t>(i) * dm + j] -= sub;
                }
            }
            std::vector<double> Hy = rep.F_cond;
            for (int i = 0; i < dy; ++i) {
                const double ly = (i < static_cast<int>(lambda_y_diag.size())) ? lambda_y_diag[static_cast<size_t>(i)] : 0.0;
                Hy[static_cast<size_t>(i) * dy + i] += ly;
            }
            rep.L_y_from_c = sym_pinv_mul(Hy, dy, rhs_miss, dm);
        }
    } else if (dm > 0 && D_miss_w) {
        auto BtDm = mul_AtB(m_obs, dy, B_Y_w, dm, D_miss_w);
        std::vector<double> Hy = rep.F_cond;
        for (int i = 0; i < dy; ++i) {
            const double ly = (i < static_cast<int>(lambda_y_diag.size())) ? lambda_y_diag[static_cast<size_t>(i)] : 0.0;
            Hy[static_cast<size_t>(i) * dy + i] += ly;
        }
        rep.L_y_from_c = sym_pinv_mul(Hy, dy, BtDm, dm);
    }

    // Symmetrize and eigendecompose F_data & F_cond
    for (int i = 0; i < dy; ++i) {
        for (int j = i + 1; j < dy; ++j) {
            const double s1 = 0.5 * (rep.F_data[static_cast<size_t>(i) * dy + j] + rep.F_data[static_cast<size_t>(j) * dy + i]);
            const double s2 = 0.5 * (rep.F_cond[static_cast<size_t>(i) * dy + j] + rep.F_cond[static_cast<size_t>(j) * dy + i]);
            rep.F_data[static_cast<size_t>(i) * dy + j] = rep.F_data[static_cast<size_t>(j) * dy + i] = s1;
            rep.F_cond[static_cast<size_t>(i) * dy + j] = rep.F_cond[static_cast<size_t>(j) * dy + i] = s2;
        }
    }
    std::vector<double> ev_d, ev_c, Vtmp;
    sym_jacobi_eigen(rep.F_data, dy, ev_d, Vtmp);
    sym_jacobi_eigen(rep.F_cond, dy, ev_c, Vtmp);
    rep.lambda_min_data = std::max(0.0, ev_d[0]);
    rep.lambda_min_cond = std::max(0.0, ev_c[0]);
    const double d_max = std::max(1e-12, ev_d.back());
    for (double v : ev_d) if (v > 1e-8 * d_max) ++rep.effective_data_rank;

    if (dm > 0 && !rep.L_y_from_c.empty()) {
        std::vector<double> LtL(static_cast<size_t>(dm) * dm, 0.0);
        for (int k = 0; k < dy; ++k)
            for (int i = 0; i < dm; ++i)
                for (int j = 0; j < dm; ++j)
                    LtL[static_cast<size_t>(i) * dm + j] +=
                        rep.L_y_from_c[static_cast<size_t>(k) * dm + i] *
                        rep.L_y_from_c[static_cast<size_t>(k) * dm + j];
        std::vector<double> ev_l;
        sym_jacobi_eigen(LtL, dm, ev_l, Vtmp);
        rep.leakage_op_norm = std::sqrt(std::max(0.0, ev_l.back()));
    }

    if (static_cast<int>(contrast_l.size()) == dy) {
        std::vector<double> Hy = rep.F_cond;
        double l_inv_prior = 0.0;
        for (int i = 0; i < dy; ++i) {
            const double ly = (i < static_cast<int>(lambda_y_diag.size())) ? std::max(1e-12, lambda_y_diag[static_cast<size_t>(i)]) : 1e-6;
            Hy[static_cast<size_t>(i) * dy + i] += ly;
            l_inv_prior += (contrast_l[static_cast<size_t>(i)] * contrast_l[static_cast<size_t>(i)]) / ly;
        }
        auto l_copy = contrast_l;
        auto Cpost_l = sym_pinv_mul(Hy, dy, l_copy, 1);
        double l_post = 0.0;
        for (int i = 0; i < dy; ++i) l_post += contrast_l[static_cast<size_t>(i)] * Cpost_l[static_cast<size_t>(i)];
        if (l_inv_prior > 1e-15) {
            rep.prior_var_reduction_s = std::clamp(1.0 - l_post / l_inv_prior, 0.0, 1.0);
        }
    }
    return rep;
}

// ---------------------------------------------------------------------------
// Q3: Convex B-Spline PSD Tile Spectral Map K(x)
// ---------------------------------------------------------------------------
// Compact 8-float per tile representation synced across CPU Stage 4/5/M5-A, Metal, and Vulkan:
//   band_gain[0..3] in [0, 1], directional SPD 2x2 tensor (k_xx, k_xy, k_yy) with 0 <= K <= I, and prior_s.
struct TileSpectralNode {
    float g_low = 1.0f;
    float g_mid = 1.0f;
    float g_hi_x = 0.0f;
    float g_hi_y = 0.0f;
    float k_xx = 0.0f;
    float k_xy = 0.0f;
    float k_yy = 0.0f;
    float leakage_bound = 0.0f;
};

struct TileSpectralMap {
    int th = 0, tw = 0;
    int tile_px = 64;
    std::vector<TileSpectralNode> nodes;

    TileSpectralNode sample_bilinear(float y_px, float x_px) const noexcept {
        if (th <= 0 || tw <= 0 || nodes.empty()) return TileSpectralNode{};
        const float gy = std::clamp(y_px / static_cast<float>(std::max(1, tile_px)) - 0.5f, 0.0f, static_cast<float>(th - 1));
        const float gx = std::clamp(x_px / static_cast<float>(std::max(1, tile_px)) - 0.5f, 0.0f, static_cast<float>(tw - 1));
        const int y0 = std::clamp(static_cast<int>(std::floor(gy)), 0, th - 1);
        const int x0 = std::clamp(static_cast<int>(std::floor(gx)), 0, tw - 1);
        const int y1 = std::min(th - 1, y0 + 1);
        const int x1 = std::min(tw - 1, x0 + 1);
        const float fy = gy - static_cast<float>(y0);
        const float fx = gx - static_cast<float>(x0);
        const float w[4] = {(1.0f - fy) * (1.0f - fx), (1.0f - fy) * fx, fy * (1.0f - fx), fy * fx};
        const TileSpectralNode* n[4] = {
            &nodes[static_cast<size_t>(y0) * tw + x0],
            &nodes[static_cast<size_t>(y0) * tw + x1],
            &nodes[static_cast<size_t>(y1) * tw + x0],
            &nodes[static_cast<size_t>(y1) * tw + x1]
        };
        TileSpectralNode o{};
        for (int i = 0; i < 4; ++i) {
            o.g_low  += w[i] * n[i]->g_low;
            o.g_mid  += w[i] * n[i]->g_mid;
            o.g_hi_x += w[i] * n[i]->g_hi_x;
            o.g_hi_y += w[i] * n[i]->g_hi_y;
            o.k_xx   += w[i] * n[i]->k_xx;
            o.k_xy   += w[i] * n[i]->k_xy;
            o.k_yy   += w[i] * n[i]->k_yy;
            o.leakage_bound += w[i] * n[i]->leakage_bound;
        }
        return o;
    }

    // Q3 convex 2D uniform cubic B-spline smoothing (w >= 0, sum == 1) preserving 0 <= K(y,x) <= I.
    TileSpectralNode sample_bspline(float y_px, float x_px) const noexcept {
        if (th <= 0 || tw <= 0 || nodes.empty()) return TileSpectralNode{};
        auto eval_b3 = [](float d) noexcept {
            const float ad = std::abs(d);
            if (ad < 1.0f) return (2.0f / 3.0f) - ad * ad + 0.5f * ad * ad * ad;
            if (ad < 2.0f) { const float t = 2.0f - ad; return (1.0f / 6.0f) * t * t * t; }
            return 0.0f;
        };
        const float gy = std::clamp(y_px / static_cast<float>(std::max(1, tile_px)) - 0.5f, 0.0f, static_cast<float>(th - 1));
        const float gx = std::clamp(x_px / static_cast<float>(std::max(1, tile_px)) - 0.5f, 0.0f, static_cast<float>(tw - 1));
        const int cy = static_cast<int>(std::floor(gy));
        const int cx = static_cast<int>(std::floor(gx));
        TileSpectralNode o{};
        o.g_low = 0.0f;
        o.g_mid = 0.0f;
        float wsum = 0.0f;
        for (int dy = -1; dy <= 2; ++dy) {
            const int iy = std::clamp(cy + dy, 0, th - 1);
            const float wy = eval_b3(gy - static_cast<float>(cy + dy));
            if (wy <= 0.0f) continue;
            for (int dx = -1; dx <= 2; ++dx) {
                const int ix = std::clamp(cx + dx, 0, tw - 1);
                const float w = wy * eval_b3(gx - static_cast<float>(cx + dx));
                if (w <= 0.0f) continue;
                const TileSpectralNode& nd = nodes[static_cast<size_t>(iy) * tw + ix];
                o.g_low  += w * nd.g_low;
                o.g_mid  += w * nd.g_mid;
                o.g_hi_x += w * nd.g_hi_x;
                o.g_hi_y += w * nd.g_hi_y;
                o.k_xx   += w * nd.k_xx;
                o.k_xy   += w * nd.k_xy;
                o.k_yy   += w * nd.k_yy;
                o.leakage_bound += w * nd.leakage_bound;
                wsum += w;
            }
        }
        if (wsum > 1e-12f) {
            const float inv = 1.0f / wsum;
            o.g_low *= inv; o.g_mid *= inv; o.g_hi_x *= inv; o.g_hi_y *= inv;
            o.k_xx *= inv; o.k_xy *= inv; o.k_yy *= inv; o.leakage_bound *= inv;
        }
        return o;
    }

    // Export tightly packed 8-float array for GPU buffers [th * tw * 8].
    std::vector<float> pack_f32() const {
        std::vector<float> buf(nodes.size() * 8u, 0.0f);
        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto& nd = nodes[i];
            buf[i * 8 + 0] = nd.g_low;
            buf[i * 8 + 1] = nd.g_mid;
            buf[i * 8 + 2] = nd.g_hi_x;
            buf[i * 8 + 3] = nd.g_hi_y;
            buf[i * 8 + 4] = nd.k_xx;
            buf[i * 8 + 5] = nd.k_xy;
            buf[i * 8 + 6] = nd.k_yy;
            buf[i * 8 + 7] = nd.leakage_bound;
        }
        return buf;
    }
};

// ---------------------------------------------------------------------------
// Q4, Q5 & R8: Stable Probit Survival, TicoRAW Lifting Adjoint & Footprint Router
// ---------------------------------------------------------------------------
inline double std_norm_pdf(double z) noexcept {
    constexpr double kInvSqrt2Pi = 0.39894228040143267794;
    return kInvSqrt2Pi * std::exp(-0.5 * z * z);
}

inline double std_norm_cdf(double z) noexcept {
    constexpr double kInvSqrt2 = 0.70710678118654752440;
    return 0.5 * std::erfc(-z * kInvSqrt2);
}

// Inverse Mills ratio m(z) = phi(z) / Phi(z) and log Phi(z), accurate cleanly down to z = -100.
inline void stable_log_phi_and_mills(double z, double& out_log_phi, double& out_mills) noexcept {
    if (z >= -6.0) {
        const double p = std_norm_cdf(z);
        out_log_phi = std::log(std::max(1e-300, p));
        out_mills = std_norm_pdf(z) / std::max(1e-300, p);
        return;
    }
    // Asymptotic expansion of Phi(z) for negative large z: Phi(z) ~ (phi(z)/|z|) * S(z),
    // where S(z) = 1 - 1/z^2 + 3/z^4 - 15/z^6 + 105/z^8.
    const double az = -z;
    const double inv_z2 = 1.0 / (z * z);
    const double series = 1.0 - inv_z2 * (1.0 - inv_z2 * (3.0 - inv_z2 * (15.0 - 105.0 * inv_z2)));
    const double log_phi_pdf = -0.91893853320467274178 - 0.5 * z * z;
    out_log_phi = log_phi_pdf - std::log(az) + std::log(std::max(1e-12, series));
    out_mills = az / std::max(1e-12, series);
}

struct ProbitScore {
    double nll = 0.0;
    double grad = 0.0;     // d(nll)/d(mu)
    double hess = 0.0;     // d^2(nll)/d(mu)^2 >= 0
};

// Q5: Upper-tail censored observation Y >= s_thr => loss = -log Phi((mu - s_thr)/sigma).
inline ProbitScore upper_tail_probit_score(double mu, double s_thr, double sigma) noexcept {
    ProbitScore out;
    const double s = std::max(1e-12, sigma);
    const double z = (mu - s_thr) / s;
    double logp = 0.0, mz = 0.0;
    stable_log_phi_and_mills(z, logp, mz);
    out.nll = -logp;
    out.grad = -mz / s;
    out.hess = std::max(0.0, mz * (z + mz) / (s * s));
    return out;
}

// Q5 Newton step with strict mixed-exposure observability gate (has_cens && has_unsat) and 1.35x cap.
inline bool apply_q5_censored_probit_step(
    double mu0, double grad, double hess, double sat_max_obs,
    bool has_cens, bool has_unsat, float& out_val
) noexcept {
    if (!(has_cens && has_unsat)) {
        out_val = static_cast<float>(mu0);
        return false;
    }
    const double step = std::clamp(grad / std::max(1e-9, hess), -0.35, 0.25);
    if (std::abs(step) <= 1e-6) {
        out_val = static_cast<float>(mu0);
        return false;
    }
    const double cand = mu0 - step;
    const double cap_hi = std::max(mu0, 1.35 * sat_max_obs);
    out_val = static_cast<float>(std::min(cand, cap_hi));
    return true;
}

// Q4: Exact 1D decision-interval probit for integer codeword preimage lo <= z < hi:
//   P_I(c) = Phi((hi - c)/sigma) - Phi((lo - c)/sigma)
//   grad  = d(-log P_I)/dc = (phi(b) - phi(a)) / (sigma * P_I)
//   hess  = (1 - Var(Z | a < Z < b)) / sigma^2
inline ProbitScore interval_box_probit_score(double c, double lo, double hi, double sigma) noexcept {
    ProbitScore out;
    const double s = std::max(1e-12, sigma);
    const double a = (lo - c) / s;
    const double b = (hi - c) / s;
    if (!(b > a)) return out;
    double P_I = 0.0;
    if (a > 0.0) {
        P_I = std_norm_cdf(-a) - std_norm_cdf(-b);
    } else if (b < 0.0) {
        P_I = std_norm_cdf(b) - std_norm_cdf(a);
    } else {
        P_I = std_norm_cdf(b) - std_norm_cdf(a);
    }
    const double ph_a = std_norm_pdf(a);
    const double ph_b = std_norm_pdf(b);
    if (P_I > 1e-15) {
        out.nll = -std::log(P_I);
        const double mean_z = (ph_a - ph_b) / P_I;
        out.grad = -mean_z / s;
        // Exact variance of truncated standard normal on [a, b]:
        // Var = 1 + (a*phi(a) - b*phi(b))/P_I - ((phi(a)-phi(b))/P_I)^2
        const double term = (a * ph_a - b * ph_b) / P_I;
        const double var_z = std::clamp(1.0 + term - mean_z * mean_z, 0.0, 1.0);
        out.hess = std::max(0.0, (1.0 - var_z) / (s * s));
    } else {
        // Fallback when interval is far out in Gaussian tail (>8 sigma)
        const double mid = 0.5 * (lo + hi);
        const double dist = (c < lo) ? (c - lo) : (c - hi);
        out.nll = 0.5 * (dist / s) * (dist / s) + 20.0;
        out.grad = (c - mid) / (s * s);
        out.hess = 1.0 / (s * s);
    }
    return out;
}

// Q4 smoothstep high-rate / deadzone blending weight alpha_b in [0, 1].
inline double compute_q4_alpha_blend(double delta_b, double sigma_b, double signal_var, double p_zero_code) noexcept {
    auto smoothstep = [](double edge0, double edge1, double x) {
        const double t = std::clamp((x - edge0) / std::max(1e-12, edge1 - edge0), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    };
    const double r_b = delta_b / std::max(1e-12, sigma_b);
    const double q_b = signal_var / std::max(1e-12, (delta_b * delta_b) / 12.0);
    const double s_r = smoothstep(0.5, 2.0, r_b);
    const double s_q = smoothstep(1.0, 9.0, q_b);
    return 1.0 - (1.0 - s_r) * (1.0 - std::clamp(p_zero_code, 0.0, 1.0) * (1.0 - s_q));
}

// 1D Biorthogonal LeGall 5/3 real-valued lifting transform L and EXACT discrete transpose L^* (L^* != L^{-1}).
// Supports arbitrary length >= 2 with symmetric Neumann mirror extension. Verified by <Lv, w> == <v, L*w>.
struct Biorthogonal53Lifting {
    static void forward(const std::vector<double>& x, std::vector<double>& y_out) {
        const int n = static_cast<int>(x.size());
        y_out.resize(static_cast<size_t>(n));
        if (n < 2) { y_out = x; return; }
        const int ne = (n + 1) / 2;
        const int no = n / 2;
        std::vector<double> even(static_cast<size_t>(ne)), odd(static_cast<size_t>(no));
        for (int i = 0; i < ne; ++i) even[static_cast<size_t>(i)] = x[static_cast<size_t>(2 * i)];
        for (int i = 0; i < no; ++i) odd[static_cast<size_t>(i)] = x[static_cast<size_t>(2 * i + 1)];
        // Step 1 (Predict odd): d[i] = odd[i] - 0.5*(even[i] + even[i+1_mirrored])
        for (int i = 0; i < no; ++i) {
            const int i_r = std::min(i + 1, ne - 1);
            odd[static_cast<size_t>(i)] -= 0.5 * (even[static_cast<size_t>(i)] + even[static_cast<size_t>(i_r)]);
        }
        // Step 2 (Update even): s[i] = even[i] + 0.25*(d[i-1_mirrored] + d[i_mirrored])
        for (int i = 0; i < ne; ++i) {
            const double dl = (i > 0) ? odd[static_cast<size_t>(i - 1)] : odd[0];
            const double dr = (i < no) ? odd[static_cast<size_t>(i)] : odd[static_cast<size_t>(no - 1)];
            even[static_cast<size_t>(i)] += 0.25 * (dl + dr);
        }
        for (int i = 0; i < ne; ++i) y_out[static_cast<size_t>(i)] = even[static_cast<size_t>(i)];
        for (int i = 0; i < no; ++i) y_out[static_cast<size_t>(ne + i)] = odd[static_cast<size_t>(i)];
    }

    static void adjoint(const std::vector<double>& y, std::vector<double>& x_adj) {
        const int n = static_cast<int>(y.size());
        x_adj.assign(static_cast<size_t>(n), 0.0);
        if (n < 2) { x_adj = y; return; }
        const int ne = (n + 1) / 2;
        const int no = n / 2;
        std::vector<double> adj_e(y.begin(), y.begin() + ne);
        std::vector<double> adj_o(y.begin() + ne, y.end());
        // Reverse Step 2 transpose: for each i in 0..ne-1, s[i] read even[i] + 0.25*(dl + dr)
        for (int i = 0; i < ne; ++i) {
            const int l_idx = (i > 0) ? (i - 1) : 0;
            const int r_idx = (i < no) ? i : (no - 1);
            const double v = 0.25 * adj_e[static_cast<size_t>(i)];
            adj_o[static_cast<size_t>(l_idx)] += v;
            adj_o[static_cast<size_t>(r_idx)] += v;
        }
        // Reverse Step 1 transpose: for each i in 0..no-1, d[i] subtracted 0.5*(even[i] + even[i_r])
        for (int i = 0; i < no; ++i) {
            const int i_r = std::min(i + 1, ne - 1);
            const double v = 0.5 * adj_o[static_cast<size_t>(i)];
            adj_e[static_cast<size_t>(i)] -= v;
            adj_e[static_cast<size_t>(i_r)] -= v;
        }
        for (int i = 0; i < ne; ++i) x_adj[static_cast<size_t>(2 * i)] = adj_e[static_cast<size_t>(i)];
        for (int i = 0; i < no; ++i) x_adj[static_cast<size_t>(2 * i + 1)] = adj_o[static_cast<size_t>(i)];
    }
};

// R8 Joint Footprint Router: decides per-coefficient support S_b whether wavelet box-probit Q4 applies
// safely or whether clipping risk triggers masking in coefficient domain so pixel upper-tail Q5 runs alone.
inline bool is_footprint_safe_for_q4(
    const double* pilot_mu,
    const double* pilot_sigma,
    const double* sat_levels,
    int footprint_len,
    double delta_tail_prob = 1e-3
) noexcept {
    if (!pilot_mu || !pilot_sigma || !sat_levels || footprint_len <= 0) return false;
    double tail_sum = 0.0;
    for (int i = 0; i < footprint_len; ++i) {
        const double z = (pilot_mu[i] - sat_levels[i]) / std::max(1e-12, pilot_sigma[i]);
        const double p_clip = std_norm_cdf(z);
        tail_sum += p_clip;
        if (tail_sum > delta_tail_prob) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Q6 & R9: Residual Noise DoF nu_r + Globally SPD Directional Sigma_H Operator
// ---------------------------------------------------------------------------
struct ResidualDoFTarget {
    double m_obs = 0.0;
    double df_fit = 0.0;   // sum h_j
    double nu_r = 0.0;     // m - 2 sum h_j + sum h_j^2
    double expected_norm_rss = 1.0; // nu_r / m
    double var_scaled_chi2 = 0.0;   // 2 * [(m - r) + sum (1 - h_j)^4]
};

inline ResidualDoFTarget compute_residual_dof(double m_obs, const std::vector<double>& h_eigs) noexcept {
    ResidualDoFTarget out;
    out.m_obs = std::max(1.0, m_obs);
    double sh = 0.0, sh2 = 0.0, s1h4 = 0.0;
    for (double h : h_eigs) {
        const double hc = std::clamp(h, 0.0, 1.0);
        sh += hc;
        sh2 += hc * hc;
        const double omh = 1.0 - hc;
        const double omh2 = omh * omh;
        s1h4 += omh2 * omh2;
    }
    const double r = static_cast<double>(h_eigs.size());
    out.df_fit = sh;
    out.nu_r = std::max(0.0, out.m_obs - 2.0 * sh + sh2);
    out.expected_norm_rss = out.nu_r / out.m_obs;
    out.var_scaled_chi2 = std::max(0.0, 2.0 * (std::max(0.0, out.m_obs - r) + s1h4));
    return out;
}

// R9: Globally SPD directional high-band marginalization R = C_n + B_H Sigma_H B_H^* > 0,
// where Sigma_H = sum_t T_t^* S_t T_t with S_t >= 0 and windowed analysis operators T_t.
inline double r9_lowband_marginal_info(
    double lowband_fisher_scalar,
    double noise_var_n,
    double highband_prior_var_uH
) noexcept {
    // Oracle for y = u_L + u_H + n with V_n = noise_var_n, Var(u_H) = highband_prior_var_uH:
    // Effective marginal noise variance R = noise_var_n + highband_prior_var_uH, info = F_L / R.
    const double R = std::max(1e-12, noise_var_n + std::max(0.0, highband_prior_var_uH));
    return lowband_fisher_scalar / R;
}

// Windowed-sinc 1D separable reconstruction basis phi_1(t) for 1x True-RGB continuous basis (Q6).
inline double windowed_sinc_luma_1x(double d, double half_width = 2.5, double passband_mul = 0.92) noexcept {
    const double ad = std::abs(d);
    if (ad >= half_width) return 0.0;
    constexpr double kPi = 3.14159265358979323846;
    const double s = (ad < 1e-8) ? 1.0 : (std::sin(kPi * passband_mul * d) / (kPi * passband_mul * d));
    const double w = 0.5 * (1.0 + std::cos(kPi * ad / half_width)); // Hann window
    return s * w;
}

// E06: Normalize user 2x --psf-sigma (fallback 1.05 -> 0.525 native 1x px) consistently for 1x and 2x grids.
inline float resolve_eff_psf_sigma_1x(
    float psf_sigma_2x,
    bool need_2x,
    float zone_sigma_grid_px = 0.0f,
    bool has_zone = false
) noexcept {
    if (has_zone && zone_sigma_grid_px > 0.0f) {
        return need_2x ? (0.5f * zone_sigma_grid_px) : zone_sigma_grid_px;
    }
    return 0.5f * psf_sigma_2x;
}

} // namespace heraw_spec
