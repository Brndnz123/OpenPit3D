// TextDrillholeData.h
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>

// ── Raw data structs (parsed directly from CSVs) ─────────────────────────────

struct Collar {
    std::string holeId;
    double x, y, z;
};

struct SurveyStation {
    std::string holeId;
    double depth;
    double azimuth;
    double dip;
};

struct SampleInterval {
    std::string holeId;
    double fromDepth;
    double toDepth;
    double grade;
};

// ── A fully desurveyed sample: 3-D midpoint + grade ───────────────────────────

struct DesurveyedSample {
    std::string holeId;
    glm::dvec3  midpoint;
    glm::dvec3  fromPt;
    glm::dvec3  toPt;
    double      grade;
    double      length;
};

// ── Spatial bucket for fast neighbour lookup ─────────────────────────────────

struct SpatialIndex {
    double cellSize = 25.0;
    std::unordered_map<int64_t, std::vector<int>> cells;
    std::vector<DesurveyedSample>* pSamples = nullptr;

    int64_t key(int cx, int cy, int cz) const {
        return ((int64_t)(cx & 0xFFFFF) << 40) |
               ((int64_t)(cy & 0xFFFFF) << 20) |
               ((int64_t)(cz & 0xFFFFF));
    }
    void insert(int idx, const glm::dvec3& pt);
    std::vector<int> query(const glm::dvec3& q, double radius) const;
};

// ── Main class — Direct CSV parsing, NO DATABASE ─────────────────────────────

class TextDrillholeData {
public:
    TextDrillholeData() = default;

    // Parse CSV files directly into memory (semicolon-delimited, header skipped)
    void importCollarsCSV  (const std::string& csvPath);
    void importSurveysCSV  (const std::string& csvPath);
    void importSamplesCSV  (const std::string& csvPath);

    std::vector<Collar>         queryCollars()  const { return collars_; }
    std::vector<SurveyStation>  querySurveys (const std::string& holeId) const;
    std::vector<SampleInterval> querySamples (const std::string& holeId) const;

    void desurveyAll();

    const std::vector<DesurveyedSample>& desurveyed()   const { return desurveyed_; }
    const SpatialIndex&                  spatialIndex() const { return spatialIdx_; }
    std::vector<std::string>             holeIds()      const;

private:
    std::vector<Collar>         collars_;
    std::vector<SurveyStation>  surveys_;
    std::vector<SampleInterval> samples_;
    std::vector<DesurveyedSample> desurveyed_;
    SpatialIndex                  spatialIdx_;

    std::vector<glm::dvec3> minimumCurvature(
        const Collar&                       collar,
        const std::vector<SurveyStation>&   surveys,
        const std::vector<double>&          queryDepths) const;
};

