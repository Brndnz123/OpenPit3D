// BlockModel.cpp
#include "BlockModel.h"
#include <cmath>
#include <algorithm>
#include <glm/gtc/constants.hpp>
#include <omp.h>

static constexpr double DEG2RAD = glm::pi<double>() / 180.0;

Eigen::Matrix3d BlockModel::buildAnisoMatrix(const VariogramStructure& st) const {
    double az  = st.azimuth * DEG2RAD;
    double dip = st.dip     * DEG2RAD;
    double pl  = st.plunge  * DEG2RAD;

    Eigen::Matrix3d Raz;
    Raz << std::cos(az), -std::sin(az), 0,
           std::sin(az),  std::cos(az), 0,
           0,             0,            1;

    Eigen::Matrix3d Rdip;
    Rdip << std::cos(dip),  0, std::sin(dip),
            0,              1, 0,
           -std::sin(dip),  0, std::cos(dip);

    Eigen::Matrix3d Rpl;
    Rpl << 1, 0,             0,
           0, std::cos(pl), -std::sin(pl),
           0, std::sin(pl),  std::cos(pl);

    Eigen::Matrix3d R = Rpl * Rdip * Raz;
    Eigen::Matrix3d S = Eigen::Matrix3d::Zero();
    S(0,0) = 1.0 / st.rangeX;
    S(1,1) = 1.0 / st.rangeY;
    S(2,2) = 1.0 / st.rangeZ;
    return S * R;
}

double BlockModel::gammaIsotropic(double h, const VariogramStructure& st) const {
    if (h <= 0.0) return 0.0;
    if (h >= 1.0) return st.sill;
    return st.sill * (1.5*h - 0.5*h*h*h);
}

double BlockModel::gamma(const glm::dvec3& hvec) const {
    double total = variogram_.nugget;
    Eigen::Vector3d h(hvec.x, hvec.y, hvec.z);
    for (const auto& st : variogram_.structures) {
        Eigen::Matrix3d M  = buildAnisoMatrix(st);
        double reducedDist = (M * h).norm();
        total += gammaIsotropic(reducedDist, st);
    }
    return total;
}

double BlockModel::gammaFromPts(const glm::dvec3& a, const glm::dvec3& b) const {
    return gamma(b - a);
}

std::vector<glm::dvec3> BlockModel::discretiseBlock(const Block& b) const {
    const int   nx = blockKriging_.nx, ny = blockKriging_.ny, nz = blockKriging_.nz;
    const double dx = blockKriging_.blockSizeX / nx;
    const double dy = blockKriging_.blockSizeY / ny;
    const double dz = blockKriging_.blockSizeZ / nz;
    const double ox = b.worldPos.x - blockKriging_.blockSizeX / 2.0;
    const double oy = b.worldPos.y - blockKriging_.blockSizeY / 2.0;
    const double oz = b.worldPos.z - blockKriging_.blockSizeZ / 2.0;

    std::vector<glm::dvec3> pts;
    pts.reserve(nx * ny * nz);
    for (int ix = 0; ix < nx; ++ix)
    for (int iy = 0; iy < ny; ++iy)
    for (int iz = 0; iz < nz; ++iz)
        pts.push_back({ ox+(ix+0.5)*dx, oy+(iy+0.5)*dy, oz+(iz+0.5)*dz });
    return pts;
}

std::vector<int> BlockModel::octantSelect(const glm::dvec3& centre,
                                           const std::vector<int>& candidates,
                                           int maxPerOctant, int maxTotal) const
{
    struct SampleDist { int idx; double dist2; };
    std::array<std::vector<SampleDist>, 8> octants;
    for (int ci : candidates) {
        glm::dvec3 d  = (*samples_)[ci].midpoint - centre;
        double dist2  = glm::dot(d,d);
        int oct = ((d.x>=0)?4:0)|((d.y>=0)?2:0)|((d.z>=0)?1:0);
        octants[oct].push_back({ci, dist2});
    }
    std::vector<int> selected;
    selected.reserve(maxTotal);
    for (auto& oct : octants) {
        std::sort(oct.begin(), oct.end(), [](const SampleDist& a, const SampleDist& b){ return a.dist2 < b.dist2; });
        int take = std::min((int)oct.size(), maxPerOctant);
        for (int i = 0; i < take; ++i) selected.push_back(oct[i].idx);
    }
    if ((int)selected.size() > maxTotal) {
        std::sort(selected.begin(), selected.end(), [&](int a, int b){
            auto da = (*samples_)[a].midpoint - centre;
            auto db = (*samples_)[b].midpoint - centre;
            return glm::dot(da,da) < glm::dot(db,db);
        });
        selected.resize(maxTotal);
    }
    return selected;
}

double BlockModel::estimateBlock(const Block& block, double* krigeVariance) const {
    if (!samples_ || !spatIdx_) return 0.0;
    const glm::dvec3 centre(block.worldPos);
    double maxRadius = std::max({search_.radiusX, search_.radiusY, search_.radiusZ});
    auto candidates  = spatIdx_->query(centre, maxRadius);

    if (!variogram_.structures.empty()) {
        VariogramStructure searchSt;
        searchSt.rangeX  = search_.radiusX; searchSt.rangeY = search_.radiusY;
        searchSt.rangeZ  = search_.radiusZ; searchSt.azimuth = search_.azimuth;
        searchSt.dip     = search_.dip;     searchSt.plunge  = search_.plunge;
        Eigen::Matrix3d M = buildAnisoMatrix(searchSt);
        std::vector<int> filtered;
        filtered.reserve(candidates.size());
        for (int ci : candidates) {
            const auto& mp = (*samples_)[ci].midpoint;
            Eigen::Vector3d h(mp.x-centre.x, mp.y-centre.y, mp.z-centre.z);
            if ((M*h).norm() <= 1.0) filtered.push_back(ci);
        }
        candidates = std::move(filtered);
    }

    auto selected = octantSelect(centre, candidates, search_.maxPerOctant, search_.maxSamples);
    int n = (int)selected.size();
    if (n < search_.minSamples) { if (krigeVariance) *krigeVariance = -1.0; return -999.0; }

    std::vector<glm::dvec3> sPos(n);
    std::vector<double>     sGrade(n);
    for (int i = 0; i < n; ++i) {
        sPos[i]   = (*samples_)[selected[i]].midpoint;
        sGrade[i] = (*samples_)[selected[i]].grade;
    }

    double totalSill = variogram_.nugget;
    for (const auto& st : variogram_.structures) totalSill += st.sill;

    auto C = [&](const glm::dvec3& a, const glm::dvec3& b) -> double {
        if (a == b) return totalSill;
        return totalSill - gammaFromPts(a, b);
    };

    Eigen::MatrixXd K(n+1, n+1);
    Eigen::VectorXd kv(n+1);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) K(i,j) = C(sPos[i], sPos[j]);
        K(i,n) = 1.0; K(n,i) = 1.0;
    }
    K(n,n) = 0.0;

    auto discPts = discretiseBlock(block);
    int  nDisc   = (int)discPts.size();
    for (int i = 0; i < n; ++i) {
        double sum = 0.0;
        for (const auto& dp : discPts) sum += C(sPos[i], dp);
        kv(i) = sum / nDisc;
    }
    kv(n) = 1.0;

    Eigen::VectorXd weights;
    Eigen::LDLT<Eigen::MatrixXd> ldlt(K);
    if (ldlt.info() == Eigen::Success) weights = ldlt.solve(kv);
    else                               weights = K.fullPivLu().solve(kv);

    double estimate = 0.0;
    for (int i = 0; i < n; ++i) estimate += weights(i) * sGrade[i];

    if (krigeVariance) {
        double cvv = 0.0;
        for (int p = 0; p < nDisc; ++p)
        for (int q = 0; q < nDisc; ++q) cvv += C(discPts[p], discPts[q]);
        cvv /= (double)(nDisc * nDisc);
        *krigeVariance = std::max(0.0, cvv - weights.head(n).dot(kv.head(n)) - weights(n));
    }

    return std::max(0.0, estimate);
}

void BlockModel::runKriging(std::vector<Block>& blocks) {
    int total = (int)blocks.size();
    #pragma omp parallel for schedule(dynamic, 32) default(none) shared(blocks, total)
    for (int i = 0; i < total; ++i) {
        if (blocks[i].state != LGState::ACTIVE) continue;
        double variance = 0.0;
        blocks[i].grade = (float)estimateBlock(blocks[i], &variance);
    }
}
