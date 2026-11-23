// heraw_preview2_modules.hpp -- Production C++20 mathematical & physical solvers
// for Preview2 principles (M2..M6), scene capabilities (N1..N6), and Sony GS/ARW2 (R11..R12).
// Pure 7-bit ASCII C++20 header.
#pragma once

#include "heraw_fs_utf8.hpp"
#include "heraw_nlens.hpp"
#include "heraw_stage4c_subspace.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace heraw_p2mod {

enum class Dtype : uint8_t {
    F64 = 1,
    C128 = 2,
    I64 = 3,
    U16 = 4,
    U8 = 5,
    BOOL = 6
};

inline size_t dtype_elem_bytes(Dtype dt) {
    switch (dt) {
        case Dtype::F64: return 8u;
        case Dtype::C128: return 16u;
        case Dtype::I64: return 8u;
        case Dtype::U16: return 2u;
        case Dtype::U8: return 1u;
        case Dtype::BOOL: return 1u;
    }
    return 0u;
}

struct Tensor {
    Dtype dtype = Dtype::F64;
    std::vector<int64_t> shape;
    std::vector<uint8_t> bytes;

    size_t numel() const {
        size_t n = 1u;
        for (int64_t d : shape) n *= static_cast<size_t>(std::max<int64_t>(0, d));
        return n;
    }

    static Tensor make_scalar_bool(bool v) {
        Tensor t;
        t.dtype = Dtype::BOOL;
        t.bytes = {static_cast<uint8_t>(v ? 1u : 0u)};
        return t;
    }

    static Tensor make_f64(std::vector<int64_t> shp, const std::vector<double>& vals) {
        Tensor t;
        t.dtype = Dtype::F64;
        t.shape = std::move(shp);
        t.bytes.resize(vals.size() * sizeof(double));
        if (!vals.empty()) std::memcpy(t.bytes.data(), vals.data(), t.bytes.size());
        return t;
    }

    static Tensor make_i64(std::vector<int64_t> shp, const std::vector<int64_t>& vals) {
        Tensor t;
        t.dtype = Dtype::I64;
        t.shape = std::move(shp);
        t.bytes.resize(vals.size() * sizeof(int64_t));
        if (!vals.empty()) std::memcpy(t.bytes.data(), vals.data(), t.bytes.size());
        return t;
    }

    static Tensor make_u16(std::vector<int64_t> shp, const std::vector<uint16_t>& vals) {
        Tensor t;
        t.dtype = Dtype::U16;
        t.shape = std::move(shp);
        t.bytes.resize(vals.size() * sizeof(uint16_t));
        if (!vals.empty()) std::memcpy(t.bytes.data(), vals.data(), t.bytes.size());
        return t;
    }

    static Tensor make_u8(std::vector<int64_t> shp, const std::vector<uint8_t>& vals, bool as_bool = false) {
        Tensor t;
        t.dtype = as_bool ? Dtype::BOOL : Dtype::U8;
        t.shape = std::move(shp);
        t.bytes = vals;
        return t;
    }

    std::vector<double> as_f64_vec() const {
        const size_t n = numel();
        std::vector<double> out(n, 0.0);
        if (dtype == Dtype::F64 && bytes.size() == n * 8u) {
            if (n > 0) std::memcpy(out.data(), bytes.data(), n * 8u);
        } else if (dtype == Dtype::I64 && bytes.size() == n * 8u) {
            for (size_t i = 0; i < n; ++i) {
                int64_t v = 0;
                std::memcpy(&v, bytes.data() + i * 8u, 8u);
                out[i] = static_cast<double>(v);
            }
        } else if (dtype == Dtype::U16 && bytes.size() == n * 2u) {
            for (size_t i = 0; i < n; ++i) {
                uint16_t v = 0;
                std::memcpy(&v, bytes.data() + i * 2u, 2u);
                out[i] = static_cast<double>(v);
            }
        } else if ((dtype == Dtype::U8 || dtype == Dtype::BOOL) && bytes.size() == n) {
            for (size_t i = 0; i < n; ++i) out[i] = static_cast<double>(bytes[i]);
        } else {
            throw std::runtime_error("Incompatible tensor dtype for f64 conversion");
        }
        return out;
    }

    std::vector<std::complex<double>> as_c128_vec() const {
        const size_t n = numel();
        std::vector<std::complex<double>> out(n);
        if (dtype == Dtype::C128 && bytes.size() == n * 16u) {
            for (size_t i = 0; i < n; ++i) {
                double re = 0.0, im = 0.0;
                std::memcpy(&re, bytes.data() + i * 16u, 8u);
                std::memcpy(&im, bytes.data() + i * 16u + 8u, 8u);
                out[i] = std::complex<double>(re, im);
            }
        } else {
            const auto rv = as_f64_vec();
            for (size_t i = 0; i < n; ++i) out[i] = std::complex<double>(rv[i], 0.0);
        }
        return out;
    }

    double scalar_f64() const {
        const auto v = as_f64_vec();
        if (v.empty()) throw std::runtime_error("Empty scalar tensor");
        return v[0];
    }

    int64_t scalar_i64() const {
        return static_cast<int64_t>(std::llround(scalar_f64()));
    }
};

using TensorMap = std::unordered_map<std::string, Tensor>;

inline TensorMap read_tensor_bundle(const std::string& path) {
    std::ifstream ifs(heraw_fs::from_utf8(path), std::ios::binary);
    if (!ifs) throw std::runtime_error("Failed opening input bundle: " + path);
    char magic[8]{};
    ifs.read(magic, 8);
    if (std::memcmp(magic, "NJP2\x01\x00\x00\x00", 8) != 0) {
        throw std::runtime_error("Invalid bundle magic");
    }
    uint32_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), 4);
    TensorMap m;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t nlen = 0;
        ifs.read(reinterpret_cast<char*>(&nlen), 4);
        std::string name(nlen, '\0');
        if (nlen > 0) ifs.read(name.data(), nlen);
        uint8_t dtc = 0;
        ifs.read(reinterpret_cast<char*>(&dtc), 1);
        uint32_t ndim = 0;
        ifs.read(reinterpret_cast<char*>(&ndim), 4);
        Tensor t;
        t.dtype = static_cast<Dtype>(dtc);
        t.shape.resize(ndim);
        for (uint32_t d = 0; d < ndim; ++d) {
            int64_t sv = 0;
            ifs.read(reinterpret_cast<char*>(&sv), 8);
            t.shape[d] = sv;
        }
        uint64_t blen = 0;
        ifs.read(reinterpret_cast<char*>(&blen), 8);
        t.bytes.resize(static_cast<size_t>(blen));
        if (blen > 0) ifs.read(reinterpret_cast<char*>(t.bytes.data()), static_cast<std::streamsize>(blen));
        if (!ifs) throw std::runtime_error("Truncated tensor bundle");
        m[std::move(name)] = std::move(t);
    }
    return m;
}

inline void write_tensor_bundle(const std::string& path, const TensorMap& m) {
    std::ofstream ofs(heraw_fs::from_utf8(path), std::ios::binary | std::ios::trunc);
    if (!ofs) throw std::runtime_error("Failed opening output bundle: " + path);
    ofs.write("NJP2\x01\x00\x00\x00", 8);
    const uint32_t count = static_cast<uint32_t>(m.size());
    ofs.write(reinterpret_cast<const char*>(&count), 4);
    for (const auto& [name, t] : m) {
        const uint32_t nlen = static_cast<uint32_t>(name.size());
        ofs.write(reinterpret_cast<const char*>(&nlen), 4);
        if (nlen > 0) ofs.write(name.data(), nlen);
        const uint8_t dtc = static_cast<uint8_t>(t.dtype);
        ofs.write(reinterpret_cast<const char*>(&dtc), 1);
        const uint32_t ndim = static_cast<uint32_t>(t.shape.size());
        ofs.write(reinterpret_cast<const char*>(&ndim), 4);
        for (int64_t sv : t.shape) ofs.write(reinterpret_cast<const char*>(&sv), 8);
        const uint64_t blen = static_cast<uint64_t>(t.bytes.size());
        ofs.write(reinterpret_cast<const char*>(&blen), 8);
        if (blen > 0) ofs.write(reinterpret_cast<const char*>(t.bytes.data()), static_cast<std::streamsize>(blen));
    }
    if (!ofs) throw std::runtime_error("Write error on output bundle");
}

// General dense linear least-squares / SPD solver for small systems
inline bool solve_dense_spd(int n, std::vector<double> A, std::vector<double>& b) {
    for (int col = 0; col < n; ++col) {
        int piv = col;
        for (int r = col + 1; r < n; ++r) {
            if (std::abs(A[size_t(r) * size_t(n) + size_t(col)]) >
                std::abs(A[size_t(piv) * size_t(n) + size_t(col)])) {
                piv = r;
            }
        }
        if (std::abs(A[size_t(piv) * size_t(n) + size_t(col)]) < 1e-15) return false;
        if (piv != col) {
            for (int c = col; c < n; ++c) {
                std::swap(A[size_t(col) * size_t(n) + size_t(c)], A[size_t(piv) * size_t(n) + size_t(c)]);
            }
            std::swap(b[size_t(col)], b[size_t(piv)]);
        }
        const double diag = A[size_t(col) * size_t(n) + size_t(col)];
        for (int r = col + 1; r < n; ++r) {
            const double f = A[size_t(r) * size_t(n) + size_t(col)] / diag;
            for (int c = col; c < n; ++c) {
                A[size_t(r) * size_t(n) + size_t(c)] -= f * A[size_t(col) * size_t(n) + size_t(c)];
            }
            b[size_t(r)] -= f * b[size_t(col)];
        }
    }
    for (int r = n - 1; r >= 0; --r) {
        double s = b[size_t(r)];
        for (int c = r + 1; c < n; ++c) s -= A[size_t(r) * size_t(n) + size_t(c)] * b[size_t(c)];
        b[size_t(r)] = s / A[size_t(r) * size_t(n) + size_t(r)];
    }
    return true;
}

// Real symmetric Jacobi eigensolver for dense (n x n) matrix: A = V * diag(evals) * V^T
inline void jacobi_sym_eig_vec(int n, std::vector<double> A, std::vector<double>& evals, std::vector<double>& V) {
    evals.assign(size_t(n), 0.0);
    V.assign(size_t(n) * size_t(n), 0.0);
    for (int i = 0; i < n; ++i) V[size_t(i) * size_t(n) + size_t(i)] = 1.0;
    for (int sweep = 0; sweep < 60; ++sweep) {
        double max_off = 0.0;
        int p = -1, q = -1;
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                const double v = std::abs(A[size_t(i) * size_t(n) + size_t(j)]);
                if (v > max_off) { max_off = v; p = i; q = j; }
            }
        }
        if (max_off < 1e-15 || p < 0) break;
        const double app = A[size_t(p) * size_t(n) + size_t(p)];
        const double aqq = A[size_t(q) * size_t(n) + size_t(q)];
        const double apq = A[size_t(p) * size_t(n) + size_t(q)];
        const double tau = (aqq - app) / (2.0 * apq);
        const double t = (tau >= 0.0)
            ? (1.0 / (tau + std::sqrt(1.0 + tau * tau)))
            : (-1.0 / (-tau + std::sqrt(1.0 + tau * tau)));
        const double c = 1.0 / std::sqrt(1.0 + t * t);
        const double s = t * c;
        for (int k = 0; k < n; ++k) {
            if (k == p || k == q) continue;
            const double akp = A[size_t(k) * size_t(n) + size_t(p)];
            const double akq = A[size_t(k) * size_t(n) + size_t(q)];
            A[size_t(k) * size_t(n) + size_t(p)] = A[size_t(p) * size_t(n) + size_t(k)] = c * akp - s * akq;
            A[size_t(k) * size_t(n) + size_t(q)] = A[size_t(q) * size_t(n) + size_t(k)] = s * akp + c * akq;
        }
        A[size_t(p) * size_t(n) + size_t(p)] = app - t * apq;
        A[size_t(q) * size_t(n) + size_t(q)] = aqq + t * apq;
        A[size_t(p) * size_t(n) + size_t(q)] = A[size_t(q) * size_t(n) + size_t(p)] = 0.0;
        for (int k = 0; k < n; ++k) {
            const double vkp = V[size_t(k) * size_t(n) + size_t(p)];
            const double vkq = V[size_t(k) * size_t(n) + size_t(q)];
            V[size_t(k) * size_t(n) + size_t(p)] = c * vkp - s * vkq;
            V[size_t(k) * size_t(n) + size_t(q)] = s * vkp + c * vkq;
        }
    }
    for (int i = 0; i < n; ++i) evals[size_t(i)] = A[size_t(i) * size_t(n) + size_t(i)];
}

// Hermitian Jacobi eigenvalues for complex Gram matrix G (D x D)
inline std::vector<double> hermitian_eigenvalues(int D, std::vector<std::complex<double>> G) {
    for (int sweep = 0; sweep < 40; ++sweep) {
        double max_off = 0.0;
        int p = -1, q = -1;
        for (int i = 0; i < D; ++i) {
            for (int j = i + 1; j < D; ++j) {
                const double mag = std::abs(G[size_t(i) * size_t(D) + size_t(j)]);
                if (mag > max_off) { max_off = mag; p = i; q = j; }
            }
        }
        if (max_off < 1e-14 || p < 0) break;
        const std::complex<double> gpq = G[size_t(p) * size_t(D) + size_t(q)];
        const double app = G[size_t(p) * size_t(D) + size_t(p)].real();
        const double aqq = G[size_t(q) * size_t(D) + size_t(q)].real();
        const double r = std::abs(gpq);
        const std::complex<double> phase = (r > 1e-30) ? (gpq / r) : std::complex<double>(1.0, 0.0);
        const double tau = (aqq - app) / (2.0 * r);
        const double t = (tau >= 0.0)
            ? (1.0 / (tau + std::sqrt(1.0 + tau * tau)))
            : (-1.0 / (-tau + std::sqrt(1.0 + tau * tau)));
        const double c = 1.0 / std::sqrt(1.0 + t * t);
        const double s = t * c;
        for (int k = 0; k < D; ++k) {
            if (k == p || k == q) continue;
            const std::complex<double> gkp = G[size_t(k) * size_t(D) + size_t(p)];
            const std::complex<double> gkq = G[size_t(k) * size_t(D) + size_t(q)];
            const std::complex<double> np = c * gkp - s * std::conj(phase) * gkq;
            const std::complex<double> nq = s * phase * gkp + c * gkq;
            G[size_t(k) * size_t(D) + size_t(p)] = np;
            G[size_t(p) * size_t(D) + size_t(k)] = std::conj(np);
            G[size_t(k) * size_t(D) + size_t(q)] = nq;
            G[size_t(q) * size_t(D) + size_t(k)] = std::conj(nq);
        }
        G[size_t(p) * size_t(D) + size_t(p)] = app - t * r;
        G[size_t(q) * size_t(D) + size_t(q)] = aqq + t * r;
        G[size_t(p) * size_t(D) + size_t(q)] = 0.0;
        G[size_t(q) * size_t(D) + size_t(p)] = 0.0;
    }
    std::vector<double> ev(size_t(D), 0.0);
    for (int i = 0; i < D; ++i) ev[size_t(i)] = std::max(0.0, G[size_t(i) * size_t(D) + size_t(i)].real());
    std::sort(ev.begin(), ev.end(), std::greater<double>());
    return ev;
}

inline bool solve_complex_lsq(
    int N,
    int M,
    const std::vector<std::complex<double>>& A,
    const std::vector<std::complex<double>>& y,
    std::vector<std::complex<double>>& z_out
) {
    // Convert M x M Hermitian normal equations (A^H A + reg I) z = A^H y to 2M x 2M real SPD system
    std::vector<double> R(size_t(2 * M) * size_t(2 * M), 0.0);
    std::vector<double> rhs(size_t(2 * M), 0.0);
    for (int i = 0; i < M; ++i) {
        std::complex<double> b_i(0.0, 0.0);
        for (int n = 0; n < N; ++n) b_i += std::conj(A[size_t(n) * size_t(M) + size_t(i)]) * y[size_t(n)];
        rhs[size_t(i)] = b_i.real();
        rhs[size_t(M + i)] = b_i.imag();
        for (int j = 0; j < M; ++j) {
            std::complex<double> g(0.0, 0.0);
            for (int n = 0; n < N; ++n) {
                g += std::conj(A[size_t(n) * size_t(M) + size_t(i)]) * A[size_t(n) * size_t(M) + size_t(j)];
            }
            if (i == j) g += 1e-13;
            R[size_t(i) * size_t(2 * M) + size_t(j)] = g.real();
            R[size_t(i) * size_t(2 * M) + size_t(M + j)] = -g.imag();
            R[size_t(M + i) * size_t(2 * M) + size_t(j)] = g.imag();
            R[size_t(M + i) * size_t(2 * M) + size_t(M + j)] = g.real();
        }
    }
    if (!solve_dense_spd(2 * M, std::move(R), rhs)) return false;
    z_out.resize(size_t(M));
    for (int i = 0; i < M; ++i) z_out[size_t(i)] = std::complex<double>(rhs[size_t(i)], rhs[size_t(M + i)]);
    return true;
}

inline double median_even_odd(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n & 1u) ? v[n / 2u] : 0.5 * (v[n / 2u - 1u] + v[n / 2u]);
}

// ============================================================================
// Principles (M2..M6) + Scenes (N2..N6) + Sony (R11..R12) Tensor Solvers
// ============================================================================
inline TensorMap solve_tensor_case(const std::string& case_id, const TensorMap& in) {
    TensorMap out;

    if (case_id == "M2-projection") {
        const auto& tA = in.at("A");
        const int rows = static_cast<int>(tA.shape.at(0));
        const int cols = static_cast<int>(tA.shape.at(1));
        const auto A = tA.as_f64_vec();
        const auto exp_v = in.at("exposure").as_f64_vec();
        const double black = in.at("black").scalar_f64();
        const double span = in.at("span").scalar_f64();
        const double white = in.at("white").scalar_f64();
        const auto dn = in.at("dn").as_f64_vec();
        const auto vis = in.at("visible").as_f64_vec();
        const double sigma_dn = in.at("sigma_dn").scalar_f64();

        std::vector<int> eq_rows, ineq_rows;
        std::vector<double> eq_rhs, ineq_lb;
        for (int r = 0; r < rows; ++r) {
            if (vis[size_t(r)] <= 0.5) continue;
            const double ex = std::max(1e-12, exp_v[size_t(r)]);
            if (heraw_s4c::is_site_nearwhite_clipped(dn[size_t(r)], white, 0.5)) {
                const double clip_norm = (white - black) * ex / span;
                const double adc_step = ex / span;
                const double sig_norm = sigma_dn * ex / span;
                const double lb = heraw_s4c::conservative_censored_lower_bound(clip_norm, adc_step, sig_norm) / (ex * ex);
                // Note: conservative_censored_lower_bound operates in anchor-normalized units where
                // clip_level = ((white - black)/span)/ex, adc_step = (1/span)/ex, sigma = (sigma_dn/span)/ex:
                const double lb_anc = heraw_s4c::conservative_censored_lower_bound(
                    (white - black) / (span * ex), 1.0 / (span * ex), sigma_dn / (span * ex));
                (void)lb;
                ineq_rows.push_back(r);
                ineq_lb.push_back(lb_anc);
            } else {
                eq_rows.push_back(r);
                eq_rhs.push_back((dn[size_t(r)] - black) / (span * ex));
            }
        }
        std::vector<double> x(size_t(cols), 0.0);
        std::vector<double> P_null(size_t(cols) * size_t(cols), 0.0);
        for (int c = 0; c < cols; ++c) P_null[size_t(c) * size_t(cols) + size_t(c)] = 1.0;

        if (!eq_rows.empty()) {
            const int meq = static_cast<int>(eq_rows.size());
            // Build symmetric Gram G = A_eq * A_eq^T (meq x meq); invariant to row permutation up to similarity.
            std::vector<double> G(size_t(meq) * size_t(meq), 0.0);
            for (int i = 0; i < meq; ++i) {
                const int ri = eq_rows[size_t(i)];
                for (int j = 0; j < meq; ++j) {
                    const int rj = eq_rows[size_t(j)];
                    double s = 0.0;
                    for (int c = 0; c < cols; ++c) {
                        s += A[size_t(ri) * size_t(cols) + size_t(c)] * A[size_t(rj) * size_t(cols) + size_t(c)];
                    }
                    G[size_t(i) * size_t(meq) + size_t(j)] = s;
                }
            }
            std::vector<double> evals, evecs;
            jacobi_sym_eig_vec(meq, G, evals, evecs);
            double emax = 0.0;
            for (double ev : evals) emax = std::max(emax, ev);
            const double etol = std::max(1e-22, 1e-12 * emax);

            // G_pinv = V * diag(1/lambda_k) * V^T
            std::vector<double> G_pinv(size_t(meq) * size_t(meq), 0.0);
            for (int k = 0; k < meq; ++k) {
                if (evals[size_t(k)] <= etol) continue;
                const double inv_l = 1.0 / evals[size_t(k)];
                for (int i = 0; i < meq; ++i) {
                    const double vi = evecs[size_t(i) * size_t(meq) + size_t(k)];
                    for (int j = 0; j < meq; ++j) {
                        G_pinv[size_t(i) * size_t(meq) + size_t(j)] +=
                            inv_l * vi * evecs[size_t(j) * size_t(meq) + size_t(k)];
                    }
                }
            }

            // Minimum-norm equality anchor x = A_eq^T * G_pinv * b_eq
            std::vector<double> y_dual(size_t(meq), 0.0);
            for (int i = 0; i < meq; ++i) {
                double s = 0.0;
                for (int j = 0; j < meq; ++j) s += G_pinv[size_t(i) * size_t(meq) + size_t(j)] * eq_rhs[size_t(j)];
                y_dual[size_t(i)] = s;
            }
            for (int c = 0; c < cols; ++c) {
                double s = 0.0;
                for (int i = 0; i < meq; ++i) {
                    s += A[size_t(eq_rows[size_t(i)]) * size_t(cols) + size_t(c)] * y_dual[size_t(i)];
                }
                x[size_t(c)] = s;
            }

            // Joint orthogonal projector P_null = I - A_eq^T * G_pinv * A_eq onto ker(A_eq)
            for (int c0 = 0; c0 < cols; ++c0) {
                for (int c1 = 0; c1 < cols; ++c1) {
                    double proj = 0.0;
                    for (int i = 0; i < meq; ++i) {
                        const double ai = A[size_t(eq_rows[size_t(i)]) * size_t(cols) + size_t(c0)];
                        for (int j = 0; j < meq; ++j) {
                            proj += ai * G_pinv[size_t(i) * size_t(meq) + size_t(j)] *
                                    A[size_t(eq_rows[size_t(j)]) * size_t(cols) + size_t(c1)];
                        }
                    }
                    P_null[size_t(c0) * size_t(cols) + size_t(c1)] =
                        (c0 == c1 ? 1.0 : 0.0) - proj;
                }
            }
        }

        // Boyle-Dykstra cyclic projection onto right-censored half-spaces inside ker(A_eq)
        struct IneqItem {
            std::vector<double> d;
            double rhs = 0.0;
            double dn2 = 0.0;
        };
        std::vector<IneqItem> hspaces;
        hspaces.reserve(ineq_rows.size());
        for (size_t k = 0; k < ineq_rows.size(); ++k) {
            const int r = ineq_rows[k];
            IneqItem it;
            it.d.assign(size_t(cols), 0.0);
            double ax0 = 0.0;
            for (int c = 0; c < cols; ++c) {
                ax0 += A[size_t(r) * size_t(cols) + size_t(c)] * x[size_t(c)];
                double dc = 0.0;
                for (int c1 = 0; c1 < cols; ++c1) {
                    dc += P_null[size_t(c) * size_t(cols) + size_t(c1)] *
                          A[size_t(r) * size_t(cols) + size_t(c1)];
                }
                it.d[size_t(c)] = dc;
                it.dn2 += dc * dc;
            }
            it.rhs = ineq_lb[k] - ax0;
            if (it.dn2 > 1e-18) hspaces.push_back(std::move(it));
        }
        std::sort(hspaces.begin(), hspaces.end(), [](const IneqItem& a, const IneqItem& b) {
            if (std::abs(a.rhs - b.rhs) > 1e-15) return a.rhs > b.rhs;
            return a.d < b.d;
        });

        std::vector<double> z(size_t(cols), 0.0);
        std::vector<std::vector<double>> dual_inc(hspaces.size(), std::vector<double>(size_t(cols), 0.0));
        for (int it = 0; it < 256; ++it) {
            double max_step = 0.0;
            for (size_t k = 0; k < hspaces.size(); ++k) {
                const auto& hs = hspaces[k];
                std::vector<double> y_in(size_t(cols), 0.0);
                double pred = 0.0;
                for (int c = 0; c < cols; ++c) {
                    y_in[size_t(c)] = z[size_t(c)] + dual_inc[k][size_t(c)];
                    pred += hs.d[size_t(c)] * y_in[size_t(c)];
                }
                std::vector<double> z_next = y_in;
                if (pred < hs.rhs) {
                    const double step = (hs.rhs - pred) / hs.dn2;
                    for (int c = 0; c < cols; ++c) z_next[size_t(c)] += step * hs.d[size_t(c)];
                }
                // Re-project onto ker(A_eq) to suppress floating-point drift
                std::vector<double> z_proj(size_t(cols), 0.0);
                for (int c0 = 0; c0 < cols; ++c0) {
                    double s = 0.0;
                    for (int c1 = 0; c1 < cols; ++c1) s += P_null[size_t(c0) * size_t(cols) + size_t(c1)] * z_next[size_t(c1)];
                    z_proj[size_t(c0)] = s;
                }
                for (int c = 0; c < cols; ++c) {
                    dual_inc[k][size_t(c)] = y_in[size_t(c)] - z_proj[size_t(c)];
                    max_step = std::max(max_step, std::abs(z_proj[size_t(c)] - z[size_t(c)]));
                    z[size_t(c)] = z_proj[size_t(c)];
                }
            }
            if (max_step < 1e-14) break;
        }
        for (int c = 0; c < cols; ++c) x[size_t(c)] += z[size_t(c)];
        out["value"] = Tensor::make_f64({static_cast<int64_t>(cols)}, x);
        return out;
    }

    if (case_id == "M2-nearwhite") {
        const auto dn = in.at("dn").as_f64_vec();
        const double clip = in.at("clip").scalar_f64();
        std::vector<double> v(dn.size(), 0.0);
        for (size_t i = 0; i < dn.size(); ++i) {
            v[i] = heraw_s4c::is_site_nearwhite_clipped(dn[i], clip, 0.5) ? 1.0 : 0.0;
        }
        out["value"] = Tensor::make_f64(in.at("dn").shape, v);
        return out;
    }

    if (case_id == "M3-airy") {
        const auto f_lp = in.at("f_lp_mm").as_f64_vec();
        const double lambda_nm = in.at("lambda_nm").scalar_f64();
        const double fno = in.at("fno").scalar_f64();
        const double last_f = in.at("last_f").scalar_f64();
        const double last_M = in.at("last_M").scalar_f64();
        const double k_tail = in.at("k_tail").scalar_f64();
        const double fc = heraw_nlens::detail::circular_pupil_cutoff_lp_mm(lambda_nm, fno);
        const double airy_fn = std::max(1e-12, heraw_nlens::detail::airy_mtf(last_f / fc));
        std::vector<double> res;
        res.reserve(1u + f_lp.size());
        res.push_back(fc);
        for (double f : f_lp) {
            if (f >= fc) {
                res.push_back(0.0);
            } else {
                const double env = heraw_nlens::detail::airy_mtf(f / fc) / airy_fn;
                res.push_back(last_M * std::exp(-k_tail * (f * f - last_f * last_f)) * env);
            }
        }
        out["value"] = Tensor::make_f64({static_cast<int64_t>(res.size())}, res);
        return out;
    }

    if (case_id == "M3-halo") {
        const auto emp = in.at("empirical").as_f64_vec();
        const auto cal = in.at("calibrated").as_f64_vec();
        const auto w = in.at("w").as_f64_vec();
        std::vector<double> res(emp.size(), 0.0);
        for (size_t i = 0; i < emp.size(); ++i) {
            res[i] = (w[i] <= 0.0) ? emp[i] : (emp[i] + w[i] * (cal[i] - emp[i]));
        }
        out["value"] = Tensor::make_f64(in.at("empirical").shape, res);
        return out;
    }

    if (case_id == "M4-framevar") {
        const auto a = in.at("a").as_f64_vec();
        const auto& tsd = in.at("code_sd");
        const int N = static_cast<int>(tsd.shape.at(0));
        const int R = static_cast<int>(tsd.shape.at(1));
        const auto sd = tsd.as_f64_vec();
        const auto jac = in.at("j").as_f64_vec();
        const auto w = in.at("w").as_f64_vec();
        const auto corr = in.at("corr").as_f64_vec();
        std::vector<double> res(size_t(R), 0.0);
        for (int r = 0; r < R; ++r) {
            std::vector<double> u(size_t(N), 0.0);
            for (int n = 0; n < N; ++n) {
                u[size_t(n)] = w[size_t(n)] * a[size_t(n)] *
                    sd[size_t(n) * size_t(R) + size_t(r)] * jac[size_t(n) * size_t(R) + size_t(r)];
            }
            double q = 0.0;
            for (int i = 0; i < N; ++i) {
                for (int j = 0; j < N; ++j) {
                    q += u[size_t(i)] * corr[size_t(i) * size_t(N) + size_t(j)] * u[size_t(j)];
                }
            }
            res[size_t(r)] = q;
        }
        out["value"] = Tensor::make_f64({static_cast<int64_t>(R)}, res);
        return out;
    }

    if (case_id == "M5-cosets") {
        const auto& tsh = in.at("shift");
        const auto& tfr = in.at("freq");
        const int N = static_cast<int>(tsh.shape.at(0));
        const int M = static_cast<int>(tfr.shape.at(0));
        const auto sh = tsh.as_f64_vec();
        const auto fr = tfr.as_f64_vec();
        const auto obs = in.at("observed").as_c128_vec();
        constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
        std::vector<std::complex<double>> A(size_t(N) * size_t(M));
        for (int n = 0; n < N; ++n) {
            for (int m = 0; m < M; ++m) {
                const double ang = -kTwoPi * (sh[size_t(n) * 2u + 0u] * fr[size_t(m) * 2u + 0u] +
                                              sh[size_t(n) * 2u + 1u] * fr[size_t(m) * 2u + 1u]);
                A[size_t(n) * size_t(M) + size_t(m)] = std::complex<double>(std::cos(ang), std::sin(ang));
            }
        }
        auto compute_rank = [&](int n_rows) -> double {
            std::vector<std::complex<double>> G(size_t(M) * size_t(M), 0.0);
            for (int i = 0; i < M; ++i) {
                for (int j = 0; j < M; ++j) {
                    std::complex<double> s(0.0, 0.0);
                    for (int n = 0; n < n_rows; ++n) {
                        s += std::conj(A[size_t(n) * size_t(M) + size_t(i)]) * A[size_t(n) * size_t(M) + size_t(j)];
                    }
                    G[size_t(i) * size_t(M) + size_t(j)] = s;
                }
            }
            const auto ev = hermitian_eigenvalues(M, std::move(G));
            const double thr = (ev.empty() ? 0.0 : ev[0]) * 1e-12;
            int rnk = 0;
            for (double v : ev) if (v > thr && v > 1e-12) ++rnk;
            return static_cast<double>(rnk);
        };
        const double rank_full = compute_rank(N);
        const double rank_half = compute_rank(std::min(N, 8));
        std::vector<std::complex<double>> z;
        solve_complex_lsq(N, M, A, obs, z);
        std::vector<double> res;
        res.reserve(2u + 2u * size_t(M));
        res.push_back(rank_full);
        res.push_back(rank_half);
        for (int m = 0; m < M; ++m) res.push_back(z[size_t(m)].real());
        for (int m = 0; m < M; ++m) res.push_back(z[size_t(m)].imag());
        out["value"] = Tensor::make_f64({static_cast<int64_t>(res.size())}, res);
        return out;
    }

    if (case_id == "M5-nyquist") {
        const auto sh = in.at("shift").as_f64_vec();
        const auto obs = in.at("observed").as_f64_vec();
        const auto coords = in.at("output_coordinates").as_f64_vec();
        double coef[3]{};
        heraw_s4c::fit_nyquist_1d_carrier(sh.data(), obs.data(), static_cast<int>(sh.size()), coef);
        constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
        std::vector<double> res(coords.size(), 0.0);
        for (size_t i = 0; i < coords.size(); ++i) {
            const double ang = kTwoPi * coords[i];
            res[i] = coef[0] + coef[1] * std::cos(ang) + coef[2] * std::sin(ang);
        }
        out["value"] = Tensor::make_f64(in.at("output_coordinates").shape, res);
        return out;
    }

    if (case_id == "M6-power") {
        const auto delta = in.at("delta").as_c128_vec();
        const auto sn = in.at("Sn").as_f64_vec();
        const auto budget = in.at("budget").as_f64_vec();
        const int64_t dc_idx = in.at("dc_index").scalar_i64();
        std::vector<double> g(delta.size(), 0.0);
        for (size_t i = 0; i < delta.size(); ++i) {
            if (static_cast<int64_t>(i) == dc_idx) {
                g[i] = 0.0;
                continue;
            }
            const double re = delta[i].real();
            const double a = std::norm(delta[i]);
            if (sn[i] <= 0.0 || a <= 1e-30) {
                g[i] = 1.0;
                continue;
            }
            const double b_rel = std::max(0.0, budget[i]) / sn[i];
            if (2.0 * re + a <= b_rel) {
                g[i] = 1.0;
            } else {
                const double disc = std::max(0.0, re * re + a * b_rel);
                g[i] = std::clamp((-re + std::sqrt(disc)) / a, 0.0, 1.0);
            }
        }
        out["value"] = Tensor::make_f64(in.at("Sn").shape, g);
        return out;
    }

    if (case_id == "M6-loo") {
        const auto n_tr = in.at("n_train").as_f64_vec();
        const double sigma = in.at("sigma").scalar_f64();
        const auto rho = in.at("exchangeable_rho").as_f64_vec();
        std::vector<double> res(rho.size() * n_tr.size(), 0.0);
        for (size_t r = 0; r < rho.size(); ++r) {
            for (size_t c = 0; c < n_tr.size(); ++c) {
                const int n = std::max(1, static_cast<int>(std::llround(n_tr[c])));
                // Evaluate w^T Sigma w where w = [1, -1/n, ..., -1/n] and Sigma_ij = sigma^2 * ((1 - rho) delta_ij + rho)
                std::vector<double> w(size_t(n + 1), -1.0 / double(n));
                w[0] = 1.0;
                const double s2 = sigma * sigma;
                double q = 0.0;
                for (int i = 0; i <= n; ++i) {
                    for (int j = 0; j <= n; ++j) {
                        const double cov = s2 * ((i == j) ? 1.0 : rho[r]);
                        q += w[size_t(i)] * cov * w[size_t(j)];
                    }
                }
                res[r * n_tr.size() + c] = q;
            }
        }
        out["value"] = Tensor::make_f64(
            {static_cast<int64_t>(rho.size()), static_cast<int64_t>(n_tr.size())}, res);
        return out;
    }

    if (case_id == "N2-subject" || case_id == "N2-ambiguous") {
        const auto& txy = in.at("xy");
        const int M = static_cast<int>(txy.shape.at(0));
        const auto xy = txy.as_f64_vec();
        const auto obs = in.at("observed").as_f64_vec();
        const auto box = in.at("af_box").as_f64_vec();
        const double bx0 = box[0], by0 = box[1], bx1 = box[2], by1 = box[3];

        std::vector<double> bg_dx, bg_dy;
        std::vector<int> inside_idx;
        for (int i = 0; i < M; ++i) {
            const double x = xy[size_t(i) * 2u + 0u], y = xy[size_t(i) * 2u + 1u];
            const double ox = obs[size_t(i) * 2u + 0u], oy = obs[size_t(i) * 2u + 1u];
            if (x >= bx0 && x <= bx1 && y >= by0 && y <= by1) {
                inside_idx.push_back(i);
            } else {
                bg_dx.push_back(ox - x);
                bg_dy.push_back(oy - y);
            }
        }
        double tx_bg = median_even_odd(bg_dx);
        double ty_bg = median_even_odd(bg_dy);
        double sx_bg = 0.0, sy_bg = 0.0;
        int cnt_bg = 0;
        for (int i = 0; i < M; ++i) {
            const double x = xy[size_t(i) * 2u + 0u], y = xy[size_t(i) * 2u + 1u];
            if (x >= bx0 && x <= bx1 && y >= by0 && y <= by1) continue;
            const double dx = obs[size_t(i) * 2u + 0u] - x;
            const double dy = obs[size_t(i) * 2u + 1u] - y;
            if (std::hypot(dx - tx_bg, dy - ty_bg) < 0.6) {
                sx_bg += dx; sy_bg += dy; ++cnt_bg;
            }
        }
        if (cnt_bg > 0) { tx_bg = sx_bg / cnt_bg; ty_bg = sy_bg / cnt_bg; }

        // Foreground candidates inside AF box whose displacement differs from background translation
        std::vector<int> fg_cand;
        for (int i : inside_idx) {
            const double dx = obs[size_t(i) * 2u + 0u] - xy[size_t(i) * 2u + 0u];
            const double dy = obs[size_t(i) * 2u + 1u] - xy[size_t(i) * 2u + 1u];
            if (std::hypot(dx - tx_bg, dy - ty_bg) > 1.2) fg_cand.push_back(i);
        }

        auto fit_similarity_2pt = [&](int i0, int i1, double& a_out, double& b_out, double& tx_out, double& ty_out) -> bool {
            const double x0 = xy[size_t(i0) * 2u], y0 = xy[size_t(i0) * 2u + 1u];
            const double x1 = xy[size_t(i1) * 2u], y1 = xy[size_t(i1) * 2u + 1u];
            const double u0 = obs[size_t(i0) * 2u], v0 = obs[size_t(i0) * 2u + 1u];
            const double u1 = obs[size_t(i1) * 2u], v1 = obs[size_t(i1) * 2u + 1u];
            const double dx = x1 - x0, dy = y1 - y0;
            const double du = u1 - u0, dv = v1 - v0;
            const double den = dx * dx + dy * dy;
            if (den < 16.0) return false;
            a_out = (dx * du + dy * dv) / den;
            b_out = (dx * dv - dy * du) / den;
            tx_out = u0 - (a_out * x0 - b_out * y0);
            ty_out = v0 - (b_out * x0 + a_out * y0);
            return true;
        };

        std::vector<int> best_inliers;
        for (size_t a = 0; a < fg_cand.size(); ++a) {
            for (size_t b = a + 1; b < fg_cand.size(); ++b) {
                double ca = 1.0, cb = 0.0, ctx = 0.0, cty = 0.0;
                if (!fit_similarity_2pt(fg_cand[a], fg_cand[b], ca, cb, ctx, cty)) continue;
                const double scale = std::hypot(ca, cb);
                if (scale < 0.7 || scale > 1.4) continue;
                std::vector<int> inl;
                for (int idx : fg_cand) {
                    const double px = ca * xy[size_t(idx) * 2u] - cb * xy[size_t(idx) * 2u + 1u] + ctx;
                    const double py = cb * xy[size_t(idx) * 2u] + ca * xy[size_t(idx) * 2u + 1u] + cty;
                    if (std::hypot(px - obs[size_t(idx) * 2u], py - obs[size_t(idx) * 2u + 1u]) < 0.40) {
                        inl.push_back(idx);
                    }
                }
                if (inl.size() > best_inliers.size()) best_inliers.swap(inl);
            }
        }

        bool active = false;
        std::vector<double> H = {1.0, 0.0, tx_bg, 0.0, 1.0, ty_bg, 0.0, 0.0, 1.0};
        if (best_inliers.size() >= 8u && best_inliers.size() * 2u >= inside_idx.size()) {
            // Refit exact 4-DOF similarity LSQ on inliers:
            // [ x -y 1 0 ] [a, b, tx, ty]^T = u
            // [ y  x 0 1 ]                  = v
            std::vector<double> ATA(16, 0.0), ATb(4, 0.0);
            for (int idx : best_inliers) {
                const double x = xy[size_t(idx) * 2u], y = xy[size_t(idx) * 2u + 1u];
                const double u = obs[size_t(idx) * 2u], v = obs[size_t(idx) * 2u + 1u];
                const double r0[4] = {x, -y, 1.0, 0.0};
                const double r1[4] = {y,  x, 0.0, 1.0};
                for (int i = 0; i < 4; ++i) {
                    ATb[size_t(i)] += r0[i] * u + r1[i] * v;
                    for (int j = 0; j < 4; ++j) {
                        ATA[size_t(i) * 4u + size_t(j)] += r0[i] * r0[j] + r1[i] * r1[j];
                    }
                }
            }
            if (solve_dense_spd(4, ATA, ATb)) {
                active = true;
                H = {ATb[0], -ATb[1], ATb[2], ATb[1], ATb[0], ATb[3], 0.0, 0.0, 1.0};
            }
        }
        out["active"] = Tensor::make_scalar_bool(active);
        out["H"] = Tensor::make_f64({3, 3}, H);
        return out;
    }

    if (case_id == "N2-roi6") {
        const auto& tcod = in.at("coded");
        const int H = static_cast<int>(tcod.shape.at(0));
        const int W = static_cast<int>(tcod.shape.at(1));
        const auto coded_f = tcod.as_f64_vec();
        const auto act = in.at("active").as_f64_vec();
        const int ax = static_cast<int>(std::llround(act[0]));
        const int ay = static_cast<int>(std::llround(act[1]));
        const int aw = static_cast<int>(std::llround(act[2]));
        const int ah = static_cast<int>(std::llround(act[3]));
        const int orient = static_cast<int>(in.at("orientation").scalar_i64());
        const auto dxywh = in.at("display_xywh").as_f64_vec();
        const int bx = static_cast<int>(std::llround(dxywh[0]));
        const int by = static_cast<int>(std::llround(dxywh[1]));
        const int bw = static_cast<int>(std::llround(dxywh[2]));
        const int bh = static_cast<int>(std::llround(dxywh[3]));
        const auto cfa_f = in.at("cfa").as_f64_vec();

        auto disp_to_active = [&](int uy, int ux, int& ar, int& ac) {
            switch (orient) {
                case 2: ar = uy; ac = aw - 1 - ux; break;
                case 3: ar = ah - 1 - uy; ac = aw - 1 - ux; break;
                case 4: ar = ah - 1 - uy; ac = ux; break;
                case 5: ar = ux; ac = uy; break;
                case 6: ar = ah - 1 - ux; ac = uy; break;            // 90 deg CW
                case 7: ar = ah - 1 - ux; ac = aw - 1 - uy; break;
                case 8: ar = ux; ac = aw - 1 - uy; break;            // 90 deg CCW
                default: ar = uy; ac = ux; break;
            }
        };

        std::vector<uint16_t> pixels(size_t(bh) * size_t(bw), 0u);
        for (int y = 0; y < bh; ++y) {
            for (int x = 0; x < bw; ++x) {
                int ar = 0, ac = 0;
                disp_to_active(by + y, bx + x, ar, ac);
                const int sr = std::clamp(ay + ar, 0, H - 1);
                const int sc = std::clamp(ax + ac, 0, W - 1);
                pixels[size_t(y) * size_t(bw) + size_t(x)] =
                    static_cast<uint16_t>(std::llround(coded_f[size_t(sr) * size_t(W) + size_t(sc)]));
            }
        }
        std::vector<uint8_t> phase(4u, 0u);
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                int ar = 0, ac = 0;
                disp_to_active(by + y, bx + x, ar, ac);
                const int sr = ay + ar, sc = ax + ac;
                phase[size_t(y) * 2u + size_t(x)] =
                    static_cast<uint8_t>(std::llround(cfa_f[size_t(sr & 1) * 2u + size_t(sc & 1)]));
            }
        }
        out["pixels"] = Tensor::make_u16({static_cast<int64_t>(bh), static_cast<int64_t>(bw)}, pixels);
        out["phase"] = Tensor::make_u8({2, 2}, phase, false);
        return out;
    }

    if (case_id == "N3-duty" || case_id == "N3-locked" || case_id == "N3-glint") {
        const auto& tf = in.at("frames");
        const int N = static_cast<int>(tf.shape.at(0));
        const int H = static_cast<int>(tf.shape.at(1));
        const int W = static_cast<int>(tf.shape.at(2));
        const auto frames = tf.as_f64_vec();
        const auto panel = in.at("panel").as_f64_vec();
        const auto ticks = in.at("phase_tick").as_f64_vec();
        const bool verified = in.at("phase_verified").scalar_i64() != 0;
        const bool stable = in.at("stable_content").scalar_i64() != 0;

        std::unordered_map<int, int> tick_counts;
        bool all_valid_ticks = true;
        for (double tv : ticks) {
            const int ti = static_cast<int>(std::llround(tv));
            if (ti < 0) { all_valid_ticks = false; break; }
            ++tick_counts[ti];
        }
        const bool eligible_burst = verified && stable && all_valid_ticks && tick_counts.size() >= 4u;
        const size_t HW = size_t(H) * size_t(W);
        std::vector<double> img(HW, 0.0);
        std::vector<uint8_t> cov(HW, 0u);
        if (!eligible_burst) {
            std::memcpy(img.data(), frames.data(), HW * sizeof(double));
        } else {
            // Balanced phase-bin average inside LED panel; preserve anchor frame[0] outside panel mask
            const double inv_K = 1.0 / static_cast<double>(tick_counts.size());
            for (size_t p = 0; p < HW; ++p) {
                if (panel[p] <= 0.5) {
                    img[p] = frames[p];
                    continue;
                }
                double vmin = 1e30, vmax = -1e30;
                for (const auto& [tk, cnt] : tick_counts) {
                    double bin_sum = 0.0;
                    for (int n = 0; n < N; ++n) {
                        const double v = frames[size_t(n) * HW + p];
                        vmin = std::min(vmin, v);
                        vmax = std::max(vmax, v);
                        if (static_cast<int>(std::llround(ticks[size_t(n)])) == tk) bin_sum += v;
                    }
                    img[p] += (bin_sum / static_cast<double>(cnt)) * inv_K;
                }
                if ((vmax - vmin) > 0.15) cov[p] = 1u;
            }
        }
        out["image"] = Tensor::make_f64({static_cast<int64_t>(H), static_cast<int64_t>(W)}, img);
        out["covered"] = Tensor::make_u8({static_cast<int64_t>(H), static_cast<int64_t>(W)}, cov, true);
        return out;
    }

    if (case_id == "N4-metamer" || case_id == "N4-neutral") {
        const auto& trgb = in.at("linear_rgb");
        const int H = static_cast<int>(trgb.shape.at(0));
        const int W = static_cast<int>(trgb.shape.at(1));
        const auto rgb = trgb.as_f64_vec();
        const auto neut = in.at("neutral_reference").as_f64_vec();

        struct Witness { double rg; double bg; double x; };
        std::vector<Witness> wit;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const size_t p = size_t(y) * size_t(W) + size_t(x);
                if (neut[p] <= 0.5) continue;
                const double r = rgb[p * 3u + 0u], g = rgb[p * 3u + 1u], b = rgb[p * 3u + 2u];
                if (g <= 1e-8) continue;
                wit.push_back({r / g, b / g, static_cast<double>(x)});
            }
        }
        double max_sep = 0.0;
        size_t i0 = 0, i1 = 0;
        for (size_t a = 0; a < wit.size(); ++a) {
            for (size_t b = a + 1; b < wit.size(); ++b) {
                const double d = std::hypot(wit[a].rg - wit[b].rg, wit[a].bg - wit[b].bg);
                if (d > max_sep) { max_sep = d; i0 = a; i1 = b; }
            }
        }
        const size_t HW = size_t(H) * size_t(W);
        std::vector<double> weights(HW, 0.0);
        bool active = false;
        if (wit.size() >= 2u && max_sep > 0.08) {
            active = true;
            // Refine 2 illuminant chromaticity clusters and order by physical CCT (warmer=0, cooler/higher B:R=1)
            double c0_rg = wit[i0].rg, c0_bg = wit[i0].bg;
            double c1_rg = wit[i1].rg, c1_bg = wit[i1].bg;
            for (int it = 0; it < 6; ++it) {
                double s0_rg = 0.0, s0_bg = 0.0; int n0 = 0;
                double s1_rg = 0.0, s1_bg = 0.0; int n1 = 0;
                for (const auto& wv : wit) {
                    const double d0 = std::hypot(wv.rg - c0_rg, wv.bg - c0_bg);
                    const double d1 = std::hypot(wv.rg - c1_rg, wv.bg - c1_bg);
                    if (d0 <= d1) { s0_rg += wv.rg; s0_bg += wv.bg; ++n0; }
                    else          { s1_rg += wv.rg; s1_bg += wv.bg; ++n1; }
                }
                if (n0 > 0 && n1 > 0) {
                    c0_rg = s0_rg / n0; c0_bg = s0_bg / n0;
                    c1_rg = s1_rg / n1; c1_bg = s1_bg / n1;
                }
            }
            if (c0_bg / std::max(1e-8, c0_rg) > c1_bg / std::max(1e-8, c1_rg)) {
                std::swap(c0_rg, c1_rg);
                std::swap(c0_bg, c1_bg);
            }
            const double ux = c1_rg - c0_rg, uy = c1_bg - c0_bg;
            const double un2 = std::max(1e-12, ux * ux + uy * uy);
            for (size_t p = 0; p < HW; ++p) {
                const double g = std::max(1e-8, rgb[p * 3u + 1u]);
                const double rg = rgb[p * 3u + 0u] / g;
                const double bg = rgb[p * 3u + 2u] / g;
                weights[p] = std::clamp(((rg - c0_rg) * ux + (bg - c0_bg) * uy) / un2, 0.0, 1.0);
            }
        }
        out["active"] = Tensor::make_scalar_bool(active);
        out["weights"] = Tensor::make_f64({static_cast<int64_t>(H), static_cast<int64_t>(W)}, weights);
        return out;
    }

    if (case_id == "N5-horizontal" || case_id == "N5-cross") {
        const auto& tf = in.at("frames");
        const int N = static_cast<int>(tf.shape.at(0));
        const int H = static_cast<int>(tf.shape.at(1));
        const int W = static_cast<int>(tf.shape.at(2));
        const auto frames = tf.as_f64_vec();
        const auto shifts = in.at("shift_xy").as_f64_vec();
        const size_t HW = size_t(H) * size_t(W);
        constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;

        // Exact 2D DFT on arbitrary (H, W)
        auto dft2_forward = [&](const double* src, std::vector<std::complex<double>>& spec) {
            std::vector<std::complex<double>> tmp(HW);
            spec.assign(HW, 0.0);
            for (int y = 0; y < H; ++y) {
                for (int kx = 0; kx < W; ++kx) {
                    std::complex<double> s(0.0, 0.0);
                    for (int x = 0; x < W; ++x) {
                        const double ang = -kTwoPi * double(kx * x) / double(W);
                        s += src[size_t(y) * size_t(W) + size_t(x)] * std::complex<double>(std::cos(ang), std::sin(ang));
                    }
                    tmp[size_t(y) * size_t(W) + size_t(kx)] = s;
                }
            }
            for (int kx = 0; kx < W; ++kx) {
                for (int ky = 0; ky < H; ++ky) {
                    std::complex<double> s(0.0, 0.0);
                    for (int y = 0; y < H; ++y) {
                        const double ang = -kTwoPi * double(ky * y) / double(H);
                        s += tmp[size_t(y) * size_t(W) + size_t(kx)] * std::complex<double>(std::cos(ang), std::sin(ang));
                    }
                    spec[size_t(ky) * size_t(W) + size_t(kx)] = s;
                }
            }
        };

        auto dft2_inverse_real = [&](const std::vector<std::complex<double>>& spec, std::vector<double>& dst) {
            std::vector<std::complex<double>> tmp(HW);
            dst.assign(HW, 0.0);
            for (int kx = 0; kx < W; ++kx) {
                for (int y = 0; y < H; ++y) {
                    std::complex<double> s(0.0, 0.0);
                    for (int ky = 0; ky < H; ++ky) {
                        const double ang = kTwoPi * double(ky * y) / double(H);
                        s += spec[size_t(ky) * size_t(W) + size_t(kx)] * std::complex<double>(std::cos(ang), std::sin(ang));
                    }
                    tmp[size_t(y) * size_t(W) + size_t(kx)] = s;
                }
            }
            const double inv_hw = 1.0 / double(HW);
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    double s = 0.0;
                    for (int kx = 0; kx < W; ++kx) {
                        const double ang = kTwoPi * double(kx * x) / double(W);
                        const auto v = tmp[size_t(y) * size_t(W) + size_t(kx)];
                        s += v.real() * std::cos(ang) - v.imag() * std::sin(ang);
                    }
                    dst[size_t(y) * size_t(W) + size_t(x)] = s * inv_hw;
                }
            }
        };

        // Check full 2D shift-diversity rank across all non-DC Fourier modes
        bool full_map = true;
        for (int ky = 0; ky < H && full_map; ++ky) {
            for (int kx = 0; kx < W; ++kx) {
                if (ky == 0 && kx == 0) continue;
                std::complex<double> phi_bar(0.0, 0.0);
                for (int n = 0; n < N; ++n) {
                    const double dx = shifts[size_t(n) * 2u + 0u];
                    const double dy = shifts[size_t(n) * 2u + 1u];
                    const double ang = -kTwoPi * (double(ky) * dy / double(H) + double(kx) * dx / double(W));
                    phi_bar += std::complex<double>(std::cos(ang), std::sin(ang));
                }
                phi_bar /= double(N);
                const double den = double(N) * (1.0 - std::norm(phi_bar));
                if (den < 1e-6) { full_map = false; break; }
            }
        }

        std::vector<double> flat(HW, 1.0);
        if (full_map) {
            std::vector<std::vector<std::complex<double>>> L_hat(static_cast<size_t>(N));
            for (int n = 0; n < N; ++n) {
                std::vector<double> log_f(HW);
                for (size_t p = 0; p < HW; ++p) {
                    log_f[p] = std::log(std::max(1e-12, frames[size_t(n) * HW + p]));
                }
                dft2_forward(log_f.data(), L_hat[size_t(n)]);
            }
            std::vector<std::complex<double>> Z_hat(HW, 0.0);
            for (int ky = 0; ky < H; ++ky) {
                for (int kx = 0; kx < W; ++kx) {
                    if (ky == 0 && kx == 0) {
                        Z_hat[0] = 0.0; // geometric mean = 1 gauge
                        continue;
                    }
                    const size_t k = size_t(ky) * size_t(W) + size_t(kx);
                    std::vector<std::complex<double>> phi(static_cast<size_t>(N));
                    std::complex<double> phi_bar(0.0, 0.0), l_bar(0.0, 0.0);
                    for (int n = 0; n < N; ++n) {
                        const double dx = shifts[size_t(n) * 2u + 0u];
                        const double dy = shifts[size_t(n) * 2u + 1u];
                        const double ang = -kTwoPi * (double(ky) * dy / double(H) + double(kx) * dx / double(W));
                        phi[size_t(n)] = std::complex<double>(std::cos(ang), std::sin(ang));
                        phi_bar += phi[size_t(n)];
                        l_bar += L_hat[size_t(n)][k];
                    }
                    phi_bar /= double(N);
                    l_bar /= double(N);
                    std::complex<double> num(0.0, 0.0);
                    double den = 0.0;
                    for (int n = 0; n < N; ++n) {
                        const auto dp = phi[size_t(n)] - phi_bar;
                        num += std::conj(dp) * (L_hat[size_t(n)][k] - l_bar);
                        den += std::norm(dp);
                    }
                    const auto S_k = num / std::max(1e-15, den);
                    Z_hat[k] = l_bar - phi_bar * S_k;
                }
            }
            std::vector<double> log_z;
            dft2_inverse_real(Z_hat, log_z);
            for (size_t p = 0; p < HW; ++p) flat[p] = std::exp(log_z[p]);
        }
        out["full_map"] = Tensor::make_scalar_bool(full_map);
        out["flat"] = Tensor::make_f64({static_cast<int64_t>(H), static_cast<int64_t>(W)}, flat);
        return out;
    }

    if (case_id == "N6-over") {
        const auto& tsf = in.at("single_frames");
        const int K = static_cast<int>(tsf.shape.at(0));
        const int H = static_cast<int>(tsf.shape.at(1));
        const int W = static_cast<int>(tsf.shape.at(2));
        const int C = static_cast<int>(tsf.shape.at(3));
        const auto sf = tsf.as_f64_vec();
        const auto& tbg = in.at("background");
        const auto bg = tbg.as_f64_vec();
        const bool bg_pixel = (bg.size() == size_t(H) * size_t(W) * size_t(C));
        const auto alpha = in.at("alpha").as_f64_vec();
        const auto ord = in.at("order").as_f64_vec();

        const size_t HW = size_t(H) * size_t(W);
        std::vector<double> comp(HW * size_t(C), 0.0);
        for (size_t p = 0; p < HW; ++p) {
            for (int c = 0; c < C; ++c) {
                comp[p * size_t(C) + size_t(c)] = bg_pixel ? bg[p * size_t(C) + size_t(c)] : bg[size_t(c)];
            }
        }
        for (double oi : ord) {
            const int k = std::clamp(static_cast<int>(std::llround(oi)), 0, K - 1);
            for (size_t p = 0; p < HW; ++p) {
                const double a = std::clamp(alpha[size_t(k) * HW + p], 0.0, 1.0);
                for (int c = 0; c < C; ++c) {
                    const double b_c = bg_pixel ? bg[p * size_t(C) + size_t(c)] : bg[size_t(c)];
                    const double f_obs = sf[(size_t(k) * HW + p) * size_t(C) + size_t(c)];
                    const double premul_fg = f_obs - (1.0 - a) * b_c;
                    comp[p * size_t(C) + size_t(c)] = premul_fg + (1.0 - a) * comp[p * size_t(C) + size_t(c)];
                }
            }
        }
        out["image"] = Tensor::make_f64(
            {static_cast<int64_t>(H), static_cast<int64_t>(W), static_cast<int64_t>(C)}, comp);
        return out;
    }

    if (case_id == "R11-beat" || case_id == "R11-flash") {
        const auto& tf = in.at("frames");
        const int N = static_cast<int>(tf.shape.at(0));
        const int H = static_cast<int>(tf.shape.at(1));
        const int W = static_cast<int>(tf.shape.at(2));
        const int C = static_cast<int>(tf.shape.at(3));
        const auto frames = tf.as_f64_vec();
        const auto rel_exp = in.at("relative_exposure").as_f64_vec();
        const auto smask = in.at("static_mask").as_f64_vec();
        const int anchor = static_cast<int>(in.at("anchor").scalar_i64());

        std::vector<double> scale(size_t(N) * size_t(C), 1.0);
        for (int ch = 0; ch < C; ++ch) {
            std::vector<std::vector<double>> row_diffs(static_cast<size_t>(N));
            for (int r = 0; r < H; ++r) {
                std::vector<int> cols;
                for (int c = 0; c < W; ++c) {
                    if (smask[size_t(r) * size_t(W) + size_t(c)] > 0.5) cols.push_back(c);
                }
                if (cols.size() < 8u) continue;
                for (int n = 0; n < N; ++n) {
                    const double inv_e = 1.0 / std::max(1e-12, rel_exp[size_t(n)]);
                    double sl = 0.0, sr = 0.0;
                    for (int k = 0; k < 4; ++k) {
                        const size_t base = (size_t(n) * size_t(H) + size_t(r)) * size_t(W);
                        sl += frames[(base + size_t(cols[size_t(k)])) * size_t(C) + size_t(ch)] * inv_e;
                        sr += frames[(base + size_t(cols[cols.size() - 4u + size_t(k)])) * size_t(C) + size_t(ch)] * inv_e;
                    }
                    row_diffs[size_t(n)].push_back(0.25 * (sr - sl));
                }
            }
            for (int n = 0; n < N; ++n) {
                scale[size_t(n) * size_t(C) + size_t(ch)] = median_even_odd(row_diffs[size_t(n)]);
            }
        }
        std::vector<double> g(size_t(N) * size_t(C), 1.0);
        double max_ptp = 0.0;
        for (int ch = 0; ch < C; ++ch) {
            const double s0 = scale[size_t(anchor) * size_t(C) + size_t(ch)];
            double gmin = 1e30, gmax = -1e30;
            for (int n = 0; n < N; ++n) {
                const double gv = s0 / scale[size_t(n) * size_t(C) + size_t(ch)];
                g[size_t(n) * size_t(C) + size_t(ch)] = gv;
                gmin = std::min(gmin, gv);
                gmax = std::max(gmax, gv);
            }
            max_ptp = std::max(max_ptp, gmax - gmin);
        }
        const bool active = (max_ptp > 0.03);
        if (!active) std::fill(g.begin(), g.end(), 1.0);
        std::vector<double> vf(size_t(N) * size_t(C), 1.0);
        for (int n = 0; n < N; ++n) {
            const double e = std::max(1e-12, rel_exp[size_t(n)]);
            for (int ch = 0; ch < C; ++ch) {
                const double r = g[size_t(n) * size_t(C) + size_t(ch)] / e;
                vf[size_t(n) * size_t(C) + size_t(ch)] = r * r;
            }
        }
        out["active"] = Tensor::make_scalar_bool(active);
        out["g"] = Tensor::make_f64({static_cast<int64_t>(N), static_cast<int64_t>(C)}, g);
        out["var_factor"] = Tensor::make_f64({static_cast<int64_t>(N), static_cast<int64_t>(C)}, vf);
        return out;
    }

    if (case_id == "R12-blocksteps") {
        const auto& tp = in.at("payload");
        const int B = static_cast<int>(tp.shape.at(0));
        if (tp.bytes.size() != size_t(B) * 16u) throw std::runtime_error("Expected Bx16 ARW2 payload");
        std::vector<int64_t> codes(size_t(B) * 16u, 0);
        std::vector<double> evar(size_t(B) * 16u, 0.0);
        for (int b = 0; b < B; ++b) {
            const uint8_t* blk = tp.bytes.data() + size_t(b) * 16u;
            __uint128_t p = 0;
            for (int k = 15; k >= 0; --k) p = (p << 8) | static_cast<__uint128_t>(blk[k]);
            const int hi = static_cast<int>(p & 2047u);
            const int lo = static_cast<int>((p >> 11) & 2047u);
            const int im = static_cast<int>((p >> 22) & 15u);
            const int il = static_cast<int>((p >> 26) & 15u);
            int step = 1;
            while (step < 16 && (hi - lo) >= 128 * step) step *= 2;
            const double qvar = static_cast<double>(step * step - 1) / 12.0;
            int bit = 30;
            for (int j = 0; j < 16; ++j) {
                if (j == im || j == il) {
                    codes[size_t(b) * 16u + size_t(j)] = (j == im) ? hi : lo;
                    evar[size_t(b) * 16u + size_t(j)] = 0.0;
                } else {
                    const int d = static_cast<int>((p >> bit) & 127u);
                    bit += 7;
                    codes[size_t(b) * 16u + size_t(j)] = std::min(2047, lo + d * step);
                    evar[size_t(b) * 16u + size_t(j)] = qvar;
                }
            }
        }
        out["codes"] = Tensor::make_i64({static_cast<int64_t>(B), 16}, codes);
        out["extra_var"] = Tensor::make_f64({static_cast<int64_t>(B), 16}, evar);
        return out;
    }

    if (case_id == "R12-lock") {
        const auto w = in.at("w").as_f64_vec();
        const double rv = in.at("read_var").scalar_f64();
        const double qv = in.at("quant_var").scalar_f64();
        const auto& trc = in.at("read_corr");
        const int K = static_cast<int>(trc.shape.at(0));
        const int N = static_cast<int>(trc.shape.at(1));
        const auto rc = trc.as_f64_vec();
        const auto qc = in.at("quant_corr").as_f64_vec();
        std::vector<double> val(size_t(K), 0.0);
        for (int k = 0; k < K; ++k) {
            double s = 0.0;
            for (int i = 0; i < N; ++i) {
                for (int j = 0; j < N; ++j) {
                    const size_t idx = (size_t(k) * size_t(N) + size_t(i)) * size_t(N) + size_t(j);
                    s += w[size_t(i)] * (rv * rc[idx] + qv * qc[idx]) * w[size_t(j)];
                }
            }
            val[size_t(k)] = s;
        }
        out["value"] = Tensor::make_f64({static_cast<int64_t>(K)}, val);
        return out;
    }

    if (case_id == "W2-PWM-shutter-mix" || case_id == "W2-PWM-period-null") {
        const auto t_start = in.at("t_start").as_f64_vec();
        const auto dur = in.at("duration").as_f64_vec();
        const double freq = in.at("frequency").scalar_f64();
        const auto& trgb = in.at("rgb");
        const int N = static_cast<int>(t_start.size());
        const int C = static_cast<int>(trgb.shape.at(1));
        const auto rgb = trgb.as_f64_vec();
        const auto t_new = in.at("t_new").as_f64_vec();
        const auto dur_new = in.at("duration_new").as_f64_vec();
        const int K_new = static_cast<int>(t_new.size());

        auto norm_sinc = [](double x) noexcept -> double {
            const double px = std::numbers::pi_v<double> * x;
            return (std::abs(px) < 1e-8) ? (1.0 - (px * px) / 6.0) : (std::sin(px) / px);
        };
        auto build_reg = [&](const std::vector<double>& tm, const std::vector<double>& dv) {
            const int R = static_cast<int>(tm.size());
            std::vector<double> M(size_t(R) * 5u, 0.0);
            for (int r = 0; r < R; ++r) {
                M[size_t(r) * 5u + 0u] = 1.0;
                for (int h = 1; h <= 2; ++h) {
                    const double mid_ang = 2.0 * std::numbers::pi_v<double> * double(h) * freq * (tm[size_t(r)] + 0.5 * dv[size_t(r)]);
                    const double tau = norm_sinc(double(h) * freq * dv[size_t(r)]);
                    M[size_t(r) * 5u + size_t(2 * h - 1)] = tau * std::cos(mid_ang);
                    M[size_t(r) * 5u + size_t(2 * h)]     = tau * std::sin(mid_ang);
                }
            }
            return M;
        };

        const auto M_tr = build_reg(t_start, dur);
        double MtM[8][8]{};
        for (int i = 0; i < 5; ++i) {
            for (int j = 0; j < 5; ++j) {
                double s = 0.0;
                for (int r = 0; r < N; ++r) s += M_tr[size_t(r) * 5u + size_t(i)] * M_tr[size_t(r) * 5u + size_t(j)];
                MtM[i][j] = s;
            }
        }
        double evals[8]{}, evecs[8][8]{};
        heraw_s4c::detail::jacobi_sym_eig<8>(5, MtM, evals, evecs);
        const double sv0 = std::sqrt(std::max(0.0, evals[0]));
        const double sv_thr = sv0 * 1e-10;
        int good_cnt = 0;
        for (int k = 0; k < 5; ++k) {
            if (std::sqrt(std::max(0.0, evals[k])) > sv_thr) ++good_cnt;
        }
        std::vector<double> beta(5u * size_t(C), 0.0);
        for (int c = 0; c < C; ++c) {
            double Mty[5]{};
            for (int i = 0; i < 5; ++i) {
                for (int r = 0; r < N; ++r) Mty[i] += M_tr[size_t(r) * 5u + size_t(i)] * rgb[size_t(r) * size_t(C) + size_t(c)];
            }
            for (int k = 0; k < 5; ++k) {
                if (std::sqrt(std::max(0.0, evals[k])) <= sv_thr) continue;
                double proj = 0.0;
                for (int i = 0; i < 5; ++i) proj += evecs[i][k] * Mty[i];
                const double w = proj / evals[k];
                for (int i = 0; i < 5; ++i) beta[size_t(i) * size_t(C) + size_t(c)] += w * evecs[i][k];
            }
        }
        std::vector<double> mean_v(size_t(C), 0.0);
        for (int c = 0; c < C; ++c) mean_v[size_t(c)] = beta[size_t(c)];

        const auto M_te = build_reg(t_new, dur_new);
        std::vector<double> mflux(size_t(K_new) * size_t(C), 0.0);
        for (int r = 0; r < K_new; ++r) {
            for (int c = 0; c < C; ++c) {
                double s = 0.0;
                for (int i = 0; i < 5; ++i) s += M_te[size_t(r) * 5u + size_t(i)] * beta[size_t(i) * size_t(C) + size_t(c)];
                mflux[size_t(r) * size_t(C) + size_t(c)] = 1000.0 * s;
            }
        }
        out["mean"] = Tensor::make_f64({static_cast<int64_t>(C)}, mean_v);
        out["rank"] = Tensor::make_i64({}, {static_cast<int64_t>(std::max(0, good_cnt - 1))});
        out["milliflux"] = Tensor::make_f64({static_cast<int64_t>(K_new), static_cast<int64_t>(C)}, mflux);
        return out;
    }

    if (case_id == "W2-cRAW-knee") {
        const auto low = in.at("low").as_f64_vec();
        const auto step = in.at("step").as_f64_vec();
        const auto upper = in.at("upper_bound").as_f64_vec();
        const auto piv = in.at("pivot").as_f64_vec();
        const auto lut = in.at("inverse_lut").as_f64_vec();
        const size_t M = low.size();
        const int lut_max = static_cast<int>(lut.size()) - 1;
        std::vector<double> mean_v(M, 0.0), var_v(M, 0.0);
        for (size_t i = 0; i < M; ++i) {
            const int lo = std::clamp(static_cast<int>(std::llround(low[i])), 0, lut_max);
            const int dl = std::max(1, static_cast<int>(std::llround(step[i])));
            const int ub = std::clamp(static_cast<int>(std::llround(upper[i])), lo, lut_max);
            const bool is_p = (piv[i] > 0.5);
            const int hi = is_p ? lo : std::min(ub, lo + dl - 1);
            const int cnt = std::max(1, hi - lo + 1);
            double s = 0.0;
            for (int c = lo; c <= hi; ++c) s += lut[size_t(c)];
            const double mu = s / double(cnt);
            double sv = 0.0;
            for (int c = lo; c <= hi; ++c) {
                const double d = lut[size_t(c)] - mu;
                sv += d * d;
            }
            mean_v[i] = mu;
            var_v[i] = sv / double(cnt);
        }
        out["mean"] = Tensor::make_f64({static_cast<int64_t>(M)}, mean_v);
        out["var"] = Tensor::make_f64({static_cast<int64_t>(M)}, var_v);
        return out;
    }

    if (case_id == "W2-LOO-correlated") {
        const auto& tf = in.at("frames");
        const int B = static_cast<int>(tf.shape.at(0));
        const int N = static_cast<int>(tf.shape.at(1));
        const auto frames = tf.as_f64_vec();
        const auto exp_v = in.at("exposures").as_f64_vec();
        const auto cov = in.at("cov").as_f64_vec();
        const auto tr = in.at("training").as_f64_vec();

        std::vector<int> idx_t;
        int idx_h = -1;
        for (int i = 0; i < N; ++i) {
            if (tr[size_t(i)] > 0.5) idx_t.push_back(i);
            else idx_h = i;
        }
        const int Nt = static_cast<int>(idx_t.size());
        if (Nt <= 0 || idx_h < 0) throw std::runtime_error("Invalid training mask in W2-LOO-correlated");
        std::vector<double> C_tt(size_t(Nt) * size_t(Nt), 0.0);
        std::vector<double> x_e(size_t(Nt), 0.0);
        for (int r = 0; r < Nt; ++r) {
            x_e[size_t(r)] = exp_v[size_t(idx_t[size_t(r)])];
            for (int c = 0; c < Nt; ++c) {
                C_tt[size_t(r) * size_t(Nt) + size_t(c)] = cov[size_t(idx_t[size_t(r)]) * size_t(N) + size_t(idx_t[size_t(c)])];
            }
        }
        if (!solve_dense_spd(Nt, C_tt, x_e)) throw std::runtime_error("Singular C_tt in W2-LOO-correlated");
        double denom = 0.0;
        for (int r = 0; r < Nt; ++r) denom += exp_v[size_t(idx_t[size_t(r)])] * x_e[size_t(r)];
        std::vector<double> w(size_t(Nt), 0.0);
        for (int r = 0; r < Nt; ++r) w[size_t(r)] = exp_v[size_t(idx_h)] * x_e[size_t(r)] / denom;

        std::vector<double> pred(size_t(B), 0.0);
        for (int b = 0; b < B; ++b) {
            double s = 0.0;
            for (int r = 0; r < Nt; ++r) s += frames[size_t(b) * size_t(N) + size_t(idx_t[size_t(r)])] * w[size_t(r)];
            pred[size_t(b)] = s;
        }
        double inn_var = cov[size_t(idx_h) * size_t(N) + size_t(idx_h)];
        for (int r = 0; r < Nt; ++r) {
            inn_var -= 2.0 * cov[size_t(idx_h) * size_t(N) + size_t(idx_t[size_t(r)])] * w[size_t(r)];
            for (int c = 0; c < Nt; ++c) {
                inn_var += w[size_t(r)] * cov[size_t(idx_t[size_t(r)]) * size_t(N) + size_t(idx_t[size_t(c)])] * w[size_t(c)];
            }
        }
        out["prediction"] = Tensor::make_f64({static_cast<int64_t>(B)}, pred);
        out["innovation_var"] = Tensor::make_f64({static_cast<int64_t>(B)}, std::vector<double>(size_t(B), inn_var));
        return out;
    }

    if (case_id == "W2-FLAT-even-dither") {
        const auto& tobs = in.at("obs");
        const int K = static_cast<int>(tobs.shape.at(0));
        const int H = static_cast<int>(tobs.shape.at(1));
        const int W = static_cast<int>(tobs.shape.at(2));
        const auto obs = tobs.as_f64_vec();
        const auto shift = in.at("shift").as_f64_vec();
        const size_t HW = size_t(H) * size_t(W);
        constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;

        auto dft2_fwd = [&](const double* img, std::vector<std::complex<double>>& F) {
            std::vector<std::complex<double>> row_ft(HW);
            for (int r = 0; r < H; ++r) {
                for (int kx = 0; kx < W; ++kx) {
                    std::complex<double> s(0.0, 0.0);
                    for (int c = 0; c < W; ++c) {
                        const double ang = -kTwoPi * double(kx * c) / double(W);
                        s += img[size_t(r) * size_t(W) + size_t(c)] * std::complex<double>(std::cos(ang), std::sin(ang));
                    }
                    row_ft[size_t(r) * size_t(W) + size_t(kx)] = s;
                }
            }
            F.assign(HW, 0.0);
            for (int kx = 0; kx < W; ++kx) {
                for (int ky = 0; ky < H; ++ky) {
                    std::complex<double> s(0.0, 0.0);
                    for (int r = 0; r < H; ++r) {
                        const double ang = -kTwoPi * double(ky * r) / double(H);
                        s += row_ft[size_t(r) * size_t(W) + size_t(kx)] * std::complex<double>(std::cos(ang), std::sin(ang));
                    }
                    F[size_t(ky) * size_t(W) + size_t(kx)] = s;
                }
            }
        };
        auto dft2_inv_real = [&](const std::vector<std::complex<double>>& F, std::vector<double>& img) {
            std::vector<std::complex<double>> col_ift(HW);
            for (int kx = 0; kx < W; ++kx) {
                for (int r = 0; r < H; ++r) {
                    std::complex<double> s(0.0, 0.0);
                    for (int ky = 0; ky < H; ++ky) {
                        const double ang = kTwoPi * double(ky * r) / double(H);
                        s += F[size_t(ky) * size_t(W) + size_t(kx)] * std::complex<double>(std::cos(ang), std::sin(ang));
                    }
                    col_ift[size_t(r) * size_t(W) + size_t(kx)] = s / double(H);
                }
            }
            img.assign(HW, 0.0);
            for (int r = 0; r < H; ++r) {
                for (int c = 0; c < W; ++c) {
                    std::complex<double> s(0.0, 0.0);
                    for (int kx = 0; kx < W; ++kx) {
                        const double ang = kTwoPi * double(kx * c) / double(W);
                        s += col_ift[size_t(r) * size_t(W) + size_t(kx)] * std::complex<double>(std::cos(ang), std::sin(ang));
                    }
                    img[size_t(r) * size_t(W) + size_t(c)] = s.real() / double(W);
                }
            }
        };

        std::vector<std::vector<std::complex<double>>> L_hat(static_cast<size_t>(K));
        for (int k = 0; k < K; ++k) {
            std::vector<double> lf(HW);
            for (size_t p = 0; p < HW; ++p) lf[p] = std::log(std::max(1e-12, obs[size_t(k) * HW + p]));
            dft2_fwd(lf.data(), L_hat[size_t(k)]);
        }
        std::vector<std::complex<double>> G_hat(HW, 0.0);
        int64_t nullity = 0;
        for (int ky = 0; ky < H; ++ky) {
            const double fy = (ky <= H / 2) ? (double(ky) / double(H)) : (double(ky - H) / double(H));
            for (int kx = 0; kx < W; ++kx) {
                const double fx = (kx <= W / 2) ? (double(kx) / double(W)) : (double(kx - W) / double(W));
                const size_t idx = size_t(ky) * size_t(W) + size_t(kx);
                std::vector<std::complex<double>> a(static_cast<size_t>(K));
                std::complex<double> a_bar(0.0, 0.0), f_bar(0.0, 0.0);
                for (int k = 0; k < K; ++k) {
                    const double dx = shift[size_t(k) * 2u + 0u];
                    const double dy = shift[size_t(k) * 2u + 1u];
                    const double ang = -kTwoPi * (dx * fx + dy * fy);
                    a[size_t(k)] = {std::cos(ang), std::sin(ang)};
                    a_bar += a[size_t(k)];
                    f_bar += L_hat[size_t(k)][idx];
                }
                a_bar /= double(K);
                f_bar /= double(K);
                std::complex<double> num(0.0, 0.0);
                double den = 0.0;
                for (int k = 0; k < K; ++k) {
                    const auto da = a[size_t(k)] - a_bar;
                    num += std::conj(da) * (L_hat[size_t(k)][idx] - f_bar);
                    den += std::norm(da);
                }
                if (den <= 1e-10) {
                    ++nullity;
                    G_hat[idx] = 0.0;
                } else {
                    const auto s_hat = num / den;
                    G_hat[idx] = f_bar - a_bar * s_hat;
                }
            }
        }
        std::vector<double> log_gain;
        dft2_inv_real(G_hat, log_gain);
        out["nullity"] = Tensor::make_i64({}, {nullity});
        out["log_gain"] = Tensor::make_f64({static_cast<int64_t>(H), static_cast<int64_t>(W)}, log_gain);
        return out;
    }

    if (case_id == "W2-OTF-complex-band") {
        const auto& totf = in.at("otf_rgb");
        const int C = static_cast<int>(totf.shape.at(0));
        const int F = static_cast<int>(totf.shape.at(1));
        const auto otf = totf.as_c128_vec();
        const auto& tw = in.at("sensor_weights");
        const int B = static_cast<int>(tw.shape.at(0));
        const auto w = tw.as_f64_vec();

        std::vector<double> val(size_t(B) * size_t(F) * 2u, 0.0);
        std::vector<uint8_t> pdef(size_t(B) * size_t(F), 0u);
        for (int b = 0; b < B; ++b) {
            double wsum = 0.0;
            for (int c = 0; c < C; ++c) wsum += w[size_t(b) * size_t(C) + size_t(c)];
            const double inv_w = (wsum > 1e-15) ? (1.0 / wsum) : 0.0;
            for (int f = 0; f < F; ++f) {
                std::complex<double> acc(0.0, 0.0);
                for (int c = 0; c < C; ++c) {
                    acc += w[size_t(b) * size_t(C) + size_t(c)] * otf[size_t(c) * size_t(F) + size_t(f)];
                }
                acc *= inv_w;
                const size_t idx = size_t(b) * size_t(F) + size_t(f);
                val[idx * 2u + 0u] = acc.real();
                val[idx * 2u + 1u] = acc.imag();
                pdef[idx] = (std::abs(acc) > 1e-7) ? 1u : 0u;
            }
        }
        out["value"] = Tensor::make_f64({static_cast<int64_t>(B), static_cast<int64_t>(F), 2}, val);
        out["phase_defined"] = Tensor::make_u8({static_cast<int64_t>(B), static_cast<int64_t>(F)}, pdef, true);
        return out;
    }

    if (case_id == "W2-GS-opposed-zones") {
        const auto& tobs = in.at("obs");
        const int T = static_cast<int>(tobs.shape.at(0));
        const int Z = static_cast<int>(tobs.shape.at(1));
        const int S = static_cast<int>(tobs.shape.at(2));
        const int C = static_cast<int>(tobs.shape.at(3));
        const auto obs = tobs.as_f64_vec();
        const auto exp_v = in.at("exposure").as_f64_vec();
        const auto smask = in.at("static").as_f64_vec();
        const int flash_f = std::clamp(static_cast<int>(in.at("flash_frame").scalar_i64()), 0, T - 1);

        std::vector<double> v_norm(obs.size(), 0.0);
        for (int t = 0; t < T; ++t) {
            const double inv_e = 1.0 / std::max(1e-12, exp_v[size_t(t)]);
            const size_t base_t = size_t(t) * size_t(Z) * size_t(S) * size_t(C);
            for (size_t i = 0; i < size_t(Z) * size_t(S) * size_t(C); ++i) {
                v_norm[base_t + i] = obs[base_t + i] * inv_e;
            }
        }
        std::vector<double> gains(size_t(T) * size_t(Z) * size_t(C), 1.0);
        for (int z = 0; z < Z; ++z) {
            for (int c = 0; c < C; ++c) {
                for (int t = 0; t < T; ++t) {
                    std::vector<double> ratios;
                    for (int s = 0; s < S; ++s) {
                        if (smask[size_t(z) * size_t(S) + size_t(s)] > 0.5) {
                            const double v0 = v_norm[((0u * size_t(Z) + size_t(z)) * size_t(S) + size_t(s)) * size_t(C) + size_t(c)];
                            const double vt = v_norm[((size_t(t) * size_t(Z) + size_t(z)) * size_t(S) + size_t(s)) * size_t(C) + size_t(c)];
                            ratios.push_back(v0 / std::max(1e-12, vt));
                        }
                    }
                    gains[(size_t(t) * size_t(Z) + size_t(z)) * size_t(C) + size_t(c)] = median_even_odd(ratios);
                }
            }
        }
        double max_zone_ptp = 0.0;
        for (int t = 0; t < T; ++t) {
            for (int c = 0; c < C; ++c) {
                double glo = 1e30, ghi = -1e30;
                for (int z = 0; z < Z; ++z) {
                    const double gv = gains[(size_t(t) * size_t(Z) + size_t(z)) * size_t(C) + size_t(c)];
                    glo = std::min(glo, gv);
                    ghi = std::max(ghi, gv);
                }
                max_zone_ptp = std::max(max_zone_ptp, ghi - glo);
            }
        }
        const int64_t global_active = (max_zone_ptp < 0.03) ? 1 : 0;

        // Evaluate transient flash contrast on non-static zone-1 green samples after per-zone gain restoration
        const int fz = std::min(1, Z - 1);
        const int fc = std::min(1, C - 1);
        double flash_sum = 0.0;
        int flash_cnt = 0;
        for (int s = 0; s < S; ++s) {
            if (smask[size_t(fz) * size_t(S) + size_t(s)] <= 0.5) {
                std::vector<double> wt(static_cast<size_t>(T), 0.0);
                for (int t = 0; t < T; ++t) {
                    const double vt = v_norm[((size_t(t) * size_t(Z) + size_t(fz)) * size_t(S) + size_t(s)) * size_t(C) + size_t(fc)];
                    const double gt = gains[(size_t(t) * size_t(Z) + size_t(fz)) * size_t(C) + size_t(fc)];
                    wt[size_t(t)] = vt * gt;
                }
                const double wf = wt[size_t(flash_f)];
                const double bg = median_even_odd(wt);
                flash_sum += (wf - bg);
                ++flash_cnt;
            }
        }
        const double ufc = (flash_cnt > 0) ? (flash_sum / double(flash_cnt)) : 0.0;
        out["global_active"] = Tensor::make_i64({}, {global_active});
        out["gain"] = Tensor::make_f64({static_cast<int64_t>(T), static_cast<int64_t>(Z), static_cast<int64_t>(C)}, gains);
        out["unchanged_flash_contrast"] = Tensor::make_f64({}, {ufc});
        return out;
    }

    throw std::runtime_error("Unknown tensor module case: " + case_id);
}

// ============================================================================
// Directory / JSON Module Solvers (N1-picks, N6-jpeg)
// ============================================================================
inline std::string json_esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4u);
    for (char c : s) {
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
        else if (c == '\n') out += "\\n";
        else out.push_back(c);
    }
    return out;
}

inline bool inspect_jpeg_header_bits(const std::filesystem::path& p, int& bits_out, bool& has_dqt_out) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (buf.size() < 4u || buf[0] != 0xFFu || buf[1] != 0xD8u) return false;
    bits_out = 8;
    has_dqt_out = false;
    size_t pos = 2u;
    while (pos + 4u <= buf.size()) {
        if (buf[pos] != 0xFFu) { ++pos; continue; }
        while (pos < buf.size() && buf[pos] == 0xFFu) ++pos;
        if (pos >= buf.size()) break;
        const uint8_t marker = buf[pos++];
        if (marker == 0xD9u || marker == 0xDAu) break;
        if (marker == 0x00u || (marker >= 0xD0u && marker <= 0xD7u)) continue;
        if (pos + 2u > buf.size()) break;
        const size_t seg_len = (size_t(buf[pos]) << 8) | size_t(buf[pos + 1u]);
        if (seg_len < 2u || pos + seg_len > buf.size()) break;
        if (marker == 0xDBu) has_dqt_out = true;
        if (marker == 0xC0u || marker == 0xC1u || marker == 0xC2u) {
            if (seg_len >= 5u) bits_out = static_cast<int>(buf[pos + 2u]);
        }
        pos += seg_len;
    }
    return true;
}

inline void solve_n6_jpeg_dir(const std::string& input_dir, const std::string& out_json_path) {
    int detected_bits = 8;
    bool all_jpeg = true;
    int file_count = 0;
    auto is_sidecar_or_meta_ext = [](const std::filesystem::path& p) -> bool {
        const std::string fname = p.filename().string();
        if (fname.empty() || fname[0] == '.') return true;
        std::string ext = p.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext == ".xmp" || ext == ".dop" || ext == ".pp3" || ext == ".json" ||
               ext == ".txt" || ext == ".xml" || ext == ".cos" || ext == ".on1";
    };
    for (const auto& ent : std::filesystem::directory_iterator(heraw_fs::from_utf8(input_dir))) {
        if (!ent.is_regular_file()) continue;
        if (is_sidecar_or_meta_ext(ent.path())) continue;
        int b = 8;
        bool has_dqt = false;
        if (inspect_jpeg_header_bits(ent.path(), b, has_dqt)) {
            detected_bits = b;
            ++file_count;
        } else {
            all_jpeg = false;
        }
    }
    const bool linear_raw = (!all_jpeg || file_count == 0);
    std::ofstream ofs(heraw_fs::from_utf8(out_json_path), std::ios::binary | std::ios::trunc);
    if (!ofs) throw std::runtime_error("Failed writing N6-jpeg output");
    ofs << "{\n"
        << "  \"source_bits\": " << detected_bits << ",\n"
        << "  \"linear_raw\": " << (linear_raw ? "true" : "false") << ",\n"
        << "  \"precision_claim\": \"" << (linear_raw ? "ADC16" : "rendered") << "\"\n"
        << "}\n";
}

} // namespace heraw_p2mod
