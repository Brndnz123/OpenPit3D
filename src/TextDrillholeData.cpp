// TextDrillholeData.cpp
#include "TextDrillholeData.h"
#include <stdexcept>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <glm/gtc/constants.hpp>

static constexpr double DEG2RAD = glm::pi<double>() / 180.0;

static glm::dvec3 surveyToDir(double azimuthDeg, double dipDeg) {
    double az  = azimuthDeg * DEG2RAD;
    double dip = dipDeg     * DEG2RAD;
    double hComp = std::cos(dip);
    return glm::dvec3(hComp * std::sin(az), -std::sin(dip), hComp * std::cos(az));
}

// ─────────────────────────────────────────────────────────────────────────────
//  SpatialIndex
// ─────────────────────────────────────────────────────────────────────────────

void SpatialIndex::insert(int idx, const glm::dvec3& pt) {
    int cx = (int)std::floor(pt.x / cellSize);
    int cy = (int)std::floor(pt.y / cellSize);
    int cz = (int)std::floor(pt.z / cellSize);
    cells[key(cx, cy, cz)].push_back(idx);
}

std::vector<int> SpatialIndex::query(const glm::dvec3& q, double radius) const {
    std::vector<int> result;
    int r  = (int)std::ceil(radius / cellSize);
    int cx = (int)std::floor(q.x / cellSize);
    int cy = (int)std::floor(q.y / cellSize);
    int cz = (int)std::floor(q.z / cellSize);
    double r2 = radius * radius;
    for (int dx = -r; dx <= r; ++dx)
    for (int dy = -r; dy <= r; ++dy)
    for (int dz = -r; dz <= r; ++dz) {
        auto it = cells.find(key(cx+dx, cy+dy, cz+dz));
        if (it == cells.end()) continue;
        for (int si : it->second) {
            glm::dvec3 d = (*pSamples)[si].midpoint - q;
            if (glm::dot(d,d) <= r2) result.push_back(si);
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  CSV parsing helpers
//
//  All three files share the same format rules:
//    • Semicolon-delimited  (;)
//    • No header row
//    • Windows line endings (\r\n) — strip trailing \r
//    • No quoted fields
//
//  splitLine() splits a line by ';' and strips '\r' from the last token.
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<std::string> splitLine(const std::string& line, char delim = ';') {
    std::vector<std::string> tokens;
    std::istringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, delim)) {
        // Strip Windows carriage return from any token (common on the last field)
        if (!tok.empty() && tok.back() == '\r') tok.pop_back();
        tokens.push_back(tok);
    }
    return tokens;
}

static double toDouble(const std::string& s) {
    return std::stod(s);
}

// ─────────────────────────────────────────────────────────────────────────────
//  File validation — only .txt accepted
// ─────────────────────────────────────────────────────────────────────────────

static void checkExtension(const std::string& path) {
    if (path.size() < 4 ||
        path.substr(path.size() - 4) != ".txt")
        throw std::runtime_error("File must have .txt extension: " + path);
}

// ─────────────────────────────────────────────────────────────────────────────
//  importCollarsCSV
//
//  Format:  hole_id ; x ; y ; z ; max_depth
//  Columns: 0         1   2   3   4  (max_depth is ignored)
//  Example: WD004;1724.725;7362.082;205.749;100.000
// ─────────────────────────────────────────────────────────────────────────────

void TextDrillholeData::importCollarsCSV(const std::string& csvPath) {
    checkExtension(csvPath);
    std::ifstream f(csvPath);
    if (!f) throw std::runtime_error("Cannot open collars: " + csvPath);

    collars_.clear();
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;

        auto t = splitLine(line);
        if (t.size() < 4) continue;   // Need at least id, x, y, z

        try {
            Collar c;
            c.holeId = t[0];
            c.x      = toDouble(t[1]);
            c.y      = toDouble(t[2]);
            c.z      = toDouble(t[3]);
            // t[4] = max_depth — not needed for desurveying
            collars_.push_back(std::move(c));
        } catch (...) {
            // Skip malformed lines silently
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  importSurveysCSV
//
//  Format:  hole_id ; depth ; dip ; azimuth
//  Columns: 0         1       2     3
//  Example: WD004;0.000;-90.000;0.000
//
//  NOTE: column order is DIP then AZIMUTH (not azimuth-first).
//  Dip convention: -90 = straight down, 0 = horizontal.
//  Our SurveyStation stores them in the standard geostatistical convention
//  (azimuth CW from North, dip positive downward), so we negate the dip here.
// ─────────────────────────────────────────────────────────────────────────────

void TextDrillholeData::importSurveysCSV(const std::string& csvPath) {
    checkExtension(csvPath);
    std::ifstream f(csvPath);
    if (!f) throw std::runtime_error("Cannot open surveys: " + csvPath);

    surveys_.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto t = splitLine(line);
        if (t.size() < 4) continue;

        try {
            SurveyStation s;
            s.holeId  = t[0];
            s.depth   = toDouble(t[1]);
            // File order: dip (negative-down convention), azimuth
            double fileDip = toDouble(t[2]);   // e.g. -90.0 for vertical
            double fileAz  = toDouble(t[3]);   // e.g. 0.0

            // Convert dip sign: file uses negative-down (surveying convention),
            // our minimum-curvature code uses positive-down (geostat convention).
            s.dip     = -fileDip;   // -90 → +90 (straight down)
            s.azimuth = fileAz;
            surveys_.push_back(std::move(s));
        } catch (...) {}
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  importSamplesCSV
//
//  Format:  hole_id ; sample_id ; from_depth ; to_depth ; grade
//  Columns: 0         1            2            3          4
//  Example: WD004;WS689231;0.000;2.000;0.030
//
//  NOTE: column 1 (sample_id) is skipped.
// ─────────────────────────────────────────────────────────────────────────────

void TextDrillholeData::importSamplesCSV(const std::string& csvPath) {
    checkExtension(csvPath);
    std::ifstream f(csvPath);
    if (!f) throw std::runtime_error("Cannot open samples: " + csvPath);

    samples_.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto t = splitLine(line);
        if (t.size() < 5) continue;   // Need id, sample_id, from, to, grade

        try {
            SampleInterval s;
            s.holeId    = t[0];
            // t[1] = sample_id  — skip
            s.fromDepth = toDouble(t[2]);
            s.toDepth   = toDouble(t[3]);
            s.grade     = toDouble(t[4]);
            samples_.push_back(std::move(s));
        } catch (...) {}
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  In-memory queries
// ─────────────────────────────────────────────────────────────────────────────

std::vector<SurveyStation> TextDrillholeData::querySurveys(const std::string& holeId) const {
    std::vector<SurveyStation> out;
    for (const auto& s : surveys_) if (s.holeId == holeId) out.push_back(s);
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b){ return a.depth < b.depth; });
    return out;
}

std::vector<SampleInterval> TextDrillholeData::querySamples(const std::string& holeId) const {
    std::vector<SampleInterval> out;
    for (const auto& s : samples_) if (s.holeId == holeId) out.push_back(s);
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b){ return a.fromDepth < b.fromDepth; });
    return out;
}

std::vector<std::string> TextDrillholeData::holeIds() const {
    std::unordered_set<std::string> ids;
    for (const auto& c : collars_) ids.insert(c.holeId);
    return {ids.begin(), ids.end()};
}

// ─────────────────────────────────────────────────────────────────────────────
//  Minimum-curvature desurveying
// ─────────────────────────────────────────────────────────────────────────────

std::vector<glm::dvec3> TextDrillholeData::minimumCurvature(
        const Collar& collar,
        const std::vector<SurveyStation>& surveys,
        const std::vector<double>& queryDepths) const
{
    struct TrajectoryPoint { double depth; glm::dvec3 pos; };
    std::vector<TrajectoryPoint> traj;
    traj.push_back({ 0.0, glm::dvec3(collar.x, collar.y, collar.z) });

    for (size_t i = 0; i + 1 < surveys.size(); ++i) {
        const auto& s1 = surveys[i];
        const auto& s2 = surveys[i + 1];
        double md    = s2.depth - s1.depth;
        double inc1  = s1.dip * DEG2RAD,   az1 = s1.azimuth * DEG2RAD;
        double inc2  = s2.dip * DEG2RAD,   az2 = s2.azimuth * DEG2RAD;
        double cosDL = std::clamp(
            std::cos(inc1)*std::cos(inc2) +
            std::sin(inc1)*std::sin(inc2)*std::cos(az2 - az1),
            -1.0, 1.0);
        double dl = std::acos(cosDL);
        double rf = (dl > 1e-6) ? (2.0 / dl) * std::tan(dl / 2.0) : 1.0;
        auto d1 = surveyToDir(s1.azimuth, s1.dip);
        auto d2 = surveyToDir(s2.azimuth, s2.dip);
        traj.push_back({ s2.depth, traj.back().pos + (md / 2.0) * rf * (d1 + d2) });
    }

    // Extrapolate from last survey as straight line if queryDepths go deeper
    std::vector<glm::dvec3> result(queryDepths.size());
    for (size_t qi = 0; qi < queryDepths.size(); ++qi) {
        double qd = queryDepths[qi];

        if (qd <= traj.front().depth) { result[qi] = traj.front().pos; continue; }

        if (qd >= traj.back().depth) {
            glm::dvec3 dir = surveys.empty()
                ? glm::dvec3(0, -1, 0)
                : surveyToDir(surveys.back().azimuth, surveys.back().dip);
            result[qi] = traj.back().pos + dir * (qd - traj.back().depth);
            continue;
        }

        // Binary search for bracket
        size_t lo = 0, hi = traj.size() - 1;
        while (hi - lo > 1) {
            size_t mid = (lo + hi) / 2;
            if (traj[mid].depth <= qd) lo = mid; else hi = mid;
        }
        double t = (qd - traj[lo].depth) / (traj[hi].depth - traj[lo].depth);
        result[qi] = glm::mix(traj[lo].pos, traj[hi].pos, t);
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  desurveyAll
// ─────────────────────────────────────────────────────────────────────────────

void TextDrillholeData::desurveyAll() {
    desurveyed_.clear();

    for (const auto& collar : collars_) {
        auto surveys = querySurveys(collar.holeId);
        auto smpls   = querySamples(collar.holeId);
        if (smpls.empty()) continue;

        std::vector<double> qd;
        qd.reserve(smpls.size() * 3);
        for (const auto& s : smpls) {
            qd.push_back(s.fromDepth);
            qd.push_back(s.toDepth);
            qd.push_back((s.fromDepth + s.toDepth) * 0.5);
        }

        auto pts = minimumCurvature(collar, surveys, qd);

        for (size_t i = 0; i < smpls.size(); ++i) {
            DesurveyedSample ds;
            ds.holeId   = collar.holeId;
            ds.fromPt   = pts[i * 3 + 0];
            ds.toPt     = pts[i * 3 + 1];
            ds.midpoint = pts[i * 3 + 2];
            ds.grade    = smpls[i].grade;
            ds.length   = glm::length(ds.toPt - ds.fromPt);
            desurveyed_.push_back(std::move(ds));
        }
    }

    // Rebuild spatial index
    spatialIdx_.cells.clear();
    spatialIdx_.pSamples = &desurveyed_;
    for (int i = 0; i < (int)desurveyed_.size(); ++i)
        spatialIdx_.insert(i, desurveyed_[i].midpoint);
}
