#include "BlockModel.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <filesystem>
#include <limits>

#ifdef _OPENMP
#include <omp.h>
#endif

// ── Kriging Yardımcı Fonksiyonları ────────────────────────────────────────────

static double sphericalVariogram(double h, double nugget, double sill, double range) {
    if (h <= 0.0001) return 0.0;
    if (h >= range) return nugget + sill;
    double hr = h / range;
    return nugget + sill * (1.5 * hr - 0.5 * hr * hr * hr);
}

// 2 Boyutlu Güvenli Matris Çözücü (Debug Abort hatasını önler)
static bool solveLinearSystem(std::vector<std::vector<double>>& A, const std::vector<double>& B, std::vector<double>& X) {
    int n = (int)B.size();
    X = B;
    for (int i = 0; i < n; i++) {
        double maxEl = std::abs(A[i][i]);
        int maxRow = i;
        for (int k = i + 1; k < n; k++) {
            if (std::abs(A[k][i]) > maxEl) {
                maxEl = std::abs(A[k][i]);
                maxRow = k;
            }
        }
        if (maxRow != i) {
            std::swap(A[maxRow], A[i]);
            std::swap(X[maxRow], X[i]);
        }
        if (std::abs(A[i][i]) < 1e-12) return false;
        for (int k = i + 1; k < n; k++) {
            double c = -A[k][i] / A[i][i];
            for (int j = i; j < n; j++) {
                if (i == j) A[k][j] = 0;
                else A[k][j] += c * A[i][j];
            }
            X[k] += c * X[i];
        }
    }
    for (int i = n - 1; i >= 0; i--) {
        X[i] = X[i] / A[i][i];
        for (int k = i - 1; k >= 0; k--) {
            X[k] -= A[k][i] * X[i];
        }
    }
    return true;
}

static float blockValue(float grade, float gc, const EconomicParams& e) {
    if (grade >= gc)
        return grade * e.recovery * (e.metalPrice - e.sellingCost)
        - e.miningCost - e.processCost;
    return -e.miningCost;
}

// ── SpatialIndex ──────────────────────────────────────────────────────────────

void SpatialIndex::insert(int idx, glm::vec3 pos) {
    auto [cx, cy, cz] = toCell(pos);
    _cells[key(cx, cy, cz)].push_back(idx);
}

std::vector<int> SpatialIndex::query(glm::vec3 pos, int r) const {
    auto [cx, cy, cz] = toCell(pos);
    std::vector<int> result;
    for (int dx = -r; dx <= r; ++dx)
        for (int dy = -r; dy <= r; ++dy)
            for (int dz = -r; dz <= r; ++dz) {
                auto it = _cells.find(key(cx + dx, cy + dy, cz + dz));
                if (it != _cells.end())
                    for (int i : it->second) result.push_back(i);
            }
    return result;
}

// ── Kurucu ve grid inşası ─────────────────────────────────────────────────────

BlockModel::BlockModel(int nx, int ny, int nz, float bs)
    : _nx(nx), _ny(ny), _nz(nz), _blockSize(bs) {
    buildGrid();
}

void BlockModel::reinitialize(int nx, int ny, int nz, float bs) {
    _nx = nx; _ny = ny; _nz = nz; _blockSize = bs;
    _blocks.clear();
    buildGrid();
    std::cout << "[BlockModel] Reinitialized: "
        << nx << "x" << ny << "x" << nz << " @ " << bs << "m\n";
}

void BlockModel::buildGrid() {
    _blocks.reserve(_nx * _ny * _nz);
    for (int iy = 0; iy < _ny; ++iy)
        for (int iz = 0; iz < _nz; ++iz)
            for (int ix = 0; ix < _nx; ++ix) {
                Block b;
                b.id = (int)_blocks.size();
                b.ix = ix; b.iy = iy; b.iz = iz;
                b.worldPos = glm::vec3(
                    (ix - _nx / 2) * _blockSize,
                    -(iy + 0.5f) * _blockSize,
                    (iz - _nz / 2) * _blockSize);
                b.grade = 0.f; b.value = 0.f;
                b.state = LGState::ACTIVE;
                _blocks.push_back(b);
            }
}

void BlockModel::resetStates() {
    for (auto& b : _blocks) b.state = LGState::ACTIVE;
}

// ── IDW (paralel) ─────────────────────────────────────────────────────────────

void BlockModel::runIDW(const std::vector<WorldPoint>& samples,
    const EconomicParams& econ, float searchRadius, int power) {
    const float gc = econ.computeCutoffGrade();
    const float cellSize = _blockSize * 2.f;
    const float sr = (searchRadius > 0.f) ? searchRadius : _blockSize * SEARCH_MULT;
    const float r2cap = sr * sr;
    const double pw = (power > 0) ? (double)power : (double)IDW_POWER;

    SpatialIndex idx(cellSize);
    for (int i = 0; i < (int)samples.size(); ++i) idx.insert(i, samples[i].pos);

    const int N = (int)_blocks.size();
    int estimated = 0;

#pragma omp parallel for schedule(dynamic, 64) reduction(+:estimated)
    for (int i = 0; i < N; ++i) {
        Block& b = _blocks[i];
        auto candidates = idx.query(b.worldPos, 3);
        double sumW = 0, sumWG = 0;
        bool hit = false;
        for (int ci : candidates) {
            glm::vec3 d = b.worldPos - samples[ci].pos;
            float d2 = glm::dot(d, d);
            if (d2 > r2cap) continue;
            if (d2 < 1e-4f) {
                b.grade = samples[ci].grade;
                b.value = blockValue(b.grade, gc, econ);
                hit = true; ++estimated; break;
            }
            double w = 1.0 / std::pow((double)d2, pw * 0.5);
            sumWG += samples[ci].grade * w;
            sumW += w;
        }
        if (!hit && sumW > 0) {
            b.grade = (float)(sumWG / sumW);
            b.value = blockValue(b.grade, gc, econ);
            ++estimated;
        }
        if (!hit && sumW == 0) b.value = -econ.miningCost;
    }
    std::cout << "[BlockModel] IDW tamamlandi: " << estimated << "/" << N << "\n";
}

// ── Nearest Neighbor ──────────────────────────────────────────────────────────

void BlockModel::runNearestNeighbor(const std::vector<WorldPoint>& samples, const EconomicParams& econ) {
    const float gc = econ.computeCutoffGrade();
    const int N = (int)_blocks.size();

#pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < N; ++i) {
        Block& b = _blocks[i];
        float bestD2 = std::numeric_limits<float>::max();
        float bestG = 0.f;
        for (const auto& s : samples) {
            glm::vec3 d = b.worldPos - s.pos;
            float d2 = glm::dot(d, d);
            if (d2 < bestD2) { bestD2 = d2; bestG = s.grade; }
        }
        b.grade = bestG;
        b.value = blockValue(b.grade, gc, econ);
    }
}

// ── Ordinary Kriging (Korumalı) ───────────────────────────────────────────────

void BlockModel::runKriging(const std::vector<WorldPoint>& samples,
    const EconomicParams& econ,
    const PitOptimizationParams& opt) {
    const float gc = econ.computeCutoffGrade();
    std::cout << "[BlockModel] Ordinary Kriging baslatildi...\n";

    const float cellSize = _blockSize * 2.f;
    const float sr = (opt.searchRadius > 0.f) ? opt.searchRadius : _blockSize * SEARCH_MULT;
    const float r2cap = sr * sr;
    const int N = (int)_blocks.size();
    int estimated = 0;

    SpatialIndex idx(cellSize);
    for (int i = 0; i < (int)samples.size(); ++i) idx.insert(i, samples[i].pos);

#pragma omp parallel for schedule(dynamic, 64) reduction(+:estimated)
    for (int i = 0; i < N; ++i) {
        try {
            Block& b = _blocks[i];
            auto candidates = idx.query(b.worldPos, 3);

            std::vector<std::pair<double, int>> dists;
            for (int ci : candidates) {
                glm::vec3 d = b.worldPos - samples[ci].pos;
                float d2 = glm::dot(d, d);
                if (d2 <= r2cap) dists.push_back({ std::sqrt(d2), ci });
            }

            std::sort(dists.begin(), dists.end(), [](const auto& a, const auto& b) {
                return a.first < b.first;
                });

            std::vector<int> selected;
            for (const auto& pair : dists) {
                bool tooClose = false;
                for (int sel_idx : selected) {
                    if (glm::distance(samples[sel_idx].pos, samples[pair.second].pos) < 0.1f) {
                        tooClose = true; break;
                    }
                }
                if (!tooClose) selected.push_back(pair.second);
                if ((int)selected.size() >= opt.krigingMaxSample) break;
            }

            int nSamples = (int)selected.size();
            if (nSamples < 3) {
                b.grade = 0.f;
                b.value = -econ.miningCost;
                continue;
            }

            int M = nSamples + 1;
            // 2D Vektör matrisi - Bounds hatası imkansız
            std::vector<std::vector<double>> A(M, std::vector<double>(M, 0.0));
            std::vector<double> B(M, 0.0);
            std::vector<double> W(M, 0.0);

            for (int r = 0; r < nSamples; r++) {
                for (int c = 0; c < nSamples; c++) {
                    double h = glm::distance(samples[selected[r]].pos, samples[selected[c]].pos);
                    A[r][c] = sphericalVariogram(h, opt.variogramNugget, opt.variogramSill, opt.variogramRange);
                }
                A[r][nSamples] = 1.0;
                A[nSamples][r] = 1.0;

                double h_target = glm::distance(b.worldPos, samples[selected[r]].pos);
                B[r] = sphericalVariogram(h_target, opt.variogramNugget, opt.variogramSill, opt.variogramRange);
            }
            A[nSamples][nSamples] = 0.0;
            B[nSamples] = 1.0;

            if (solveLinearSystem(A, B, W)) {
                double estimatedGrade = 0.0;
                for (int r = 0; r < nSamples; r++) {
                    estimatedGrade += W[r] * samples[selected[r]].grade;
                }
                b.grade = std::max(0.0f, (float)estimatedGrade);
                b.value = blockValue(b.grade, gc, econ);
                ++estimated;
            }
            else {
                b.grade = 0.f;
                b.value = -econ.miningCost;
            }
        }
        catch (...) {
            // Hata çıkarsa programın çökmesini engelle
            _blocks[i].grade = 0.f;
            _blocks[i].value = -econ.miningCost;
        }
    }
    std::cout << "[BlockModel] Kriging tamamlandi: " << estimated << "/" << N << " blok.\n";
}

// ── Ana kestirim giriş noktası ────────────────────────────────────────────────

void BlockModel::estimateFromDatabase(const DrillholeDatabase& db,
    const EconomicParams& econ,
    const PitOptimizationParams& opt) {
    resetStates();
    auto samples = db.getDesurveyedSamples();
    if (samples.empty()) {
        std::cerr << "[BlockModel] Ornek yok — collar + sample dosyalari yukleyin.\n";
        return;
    }
    switch (opt.estimMethod) {
    case EstimationMethod::IDW:
        runIDW(samples, econ, opt.searchRadius, opt.idwPower);
        break;
    case EstimationMethod::NEAREST_NEIGHBOR:
        runNearestNeighbor(samples, econ);
        break;
    case EstimationMethod::KRIGING:
        runKriging(samples, econ, opt);
        break;
    }
}

// ── CSV Raporu ────────────────────────────────────────────────────────────────

bool BlockModel::generateCSVReport(const std::string& filepath, const EconomicParams& econ, float rockDensity) const {
    const float metalPrice = (econ.metalPrice > 0.f) ? econ.metalPrice : 100.f;
    const float recovery = (econ.recovery > 0.f && econ.recovery <= 1.f) ? econ.recovery : 0.90f;
    const float miningCost = (econ.miningCost > 0.f) ? econ.miningCost : 30.f;
    const float processCost = (econ.processCost >= 0.f) ? econ.processCost : 20.f;
    const float sellingCost = (econ.sellingCost >= 0.f) ? econ.sellingCost : 5.f;

    float gc = 0.f;
    float netSmelter = metalPrice * recovery - miningCost;
    if (netSmelter > 0.f) {
        float g_m = (processCost + sellingCost) / netSmelter;
        float g_p = processCost / netSmelter;
        float g_k = (processCost + sellingCost) / (metalPrice * recovery);
        gc = std::max({ std::min(g_m, g_p), std::min(g_p, g_k), std::min(g_m, g_k) });
        gc = std::max(0.f, gc);
    }

    int nInPit = 0, nMined = 0;
    for (const auto& b : _blocks) {
        if (b.state == LGState::IN_PIT) ++nInPit;
        if (b.state == LGState::MINED) ++nMined;
    }
    if (nInPit == 0 && nMined == 0) return false;

    const float blockVol = _blockSize * _blockSize * _blockSize;
    const float blockTonnes = blockVol * rockDensity;
    double oreTonnes = 0, wasteTonnes = 0, sumGradeOre = 0, netNPV = 0;
    long   oreBlocks = 0, wasteBlocks = 0;

    for (const auto& b : _blocks) {
        if (b.state != LGState::IN_PIT && b.state != LGState::MINED) continue;
        netNPV += (double)b.value;
        if (b.grade >= gc) {
            oreTonnes += blockTonnes; sumGradeOre += b.grade; ++oreBlocks;
        }
        else {
            wasteTonnes += blockTonnes; ++wasteBlocks;
        }
    }

    const double avgGrade = (oreBlocks > 0) ? sumGradeOre / oreBlocks : 0.0;
    const double stripRatio = (oreTonnes > 0) ? wasteTonnes / oreTonnes : 0.0;

    std::ofstream f(filepath);
    if (!f.is_open()) return false;

    f << std::fixed;
    f << "LABEL,VALUE,UNIT\n";
    f << "--- PIT OZETI ---,,\n";
    f << "Ore Blok," << oreBlocks << ",adet\n";
    f << "Waste Blok," << wasteBlocks << ",adet\n";
    f << "Ore Tonaj," << std::setprecision(1) << oreTonnes << ",t\n";
    f << "Waste Tonaj," << std::setprecision(1) << wasteTonnes << ",t\n";
    f << "Stripping Orani," << std::setprecision(3) << stripRatio << ",t waste/t ore\n";
    f << "Ort. Ore Tenoru," << std::setprecision(4) << avgGrade << ",%\n";
    f << "Net Pit Degeri," << std::setprecision(2) << netNPV << ",$\n";
    f.close();
    return true;
}