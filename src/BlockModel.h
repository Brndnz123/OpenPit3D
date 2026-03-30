// BlockModel.h
#pragma once
#include <vector>
#include <array>
#include <glm/glm.hpp>
#include <Eigen/Dense>
#include "Types.h"
#include "TextDrillholeData.h"

struct VariogramStructure {
    double sill   = 1.0;
    double rangeX = 200.0;
    double rangeY = 100.0;
    double rangeZ = 50.0;
    double azimuth = 0.0;
    double dip     = 0.0;
    double plunge  = 0.0;
};

struct VariogramModel {
    double nugget = 0.05;
    std::vector<VariogramStructure> structures;
};

struct SearchParams {
    double radiusX = 200.0;
    double radiusY = 100.0;
    double radiusZ = 50.0;
    double azimuth = 0.0;
    double dip     = 0.0;
    double plunge  = 0.0;
    int    minSamples   = 4;
    int    maxSamples   = 16;
    int    maxPerOctant = 3;
};

struct BlockKrigingParams {
    int    nx = 4, ny = 4, nz = 4;
    double blockSizeX = 20.0;
    double blockSizeY = 20.0;
    double blockSizeZ = 20.0;
};

class BlockModel {
public:
    // Expose types so SceneController can pass them by value to the worker
    using VariogramModel    = ::VariogramModel;
    using SearchParams      = ::SearchParams;
    using BlockKrigingParams = ::BlockKrigingParams;

    BlockModel() = default;

    void setVariogram    (const VariogramModel&     vm) { variogram_    = vm; }
    void setSearchParams (const SearchParams&       sp) { search_       = sp; }
    void setKrigingParams(const BlockKrigingParams& bp) { blockKriging_ = bp; }
    void setSamples(const std::vector<DesurveyedSample>* s,
                    const SpatialIndex*                  idx) {
        samples_ = s; spatIdx_ = idx;
    }

    double estimateBlock(const Block& block, double* krigeVariance = nullptr) const;
    void   runKriging(std::vector<Block>& blocks);

private:
    VariogramModel     variogram_;
    SearchParams       search_;
    BlockKrigingParams blockKriging_;
    const std::vector<DesurveyedSample>* samples_ = nullptr;
    const SpatialIndex*                  spatIdx_ = nullptr;

    double gammaIsotropic(double reducedDist, const VariogramStructure& st) const;
    double gamma(const glm::dvec3& h) const;
    double gammaFromPts(const glm::dvec3& a, const glm::dvec3& b) const;
    Eigen::Matrix3d buildAnisoMatrix(const VariogramStructure& st) const;
    std::vector<int> octantSelect(const glm::dvec3& centre,
                                  const std::vector<int>& candidates,
                                  int maxPerOctant, int maxTotal) const;
    std::vector<glm::dvec3> discretiseBlock(const Block& block) const;
};
