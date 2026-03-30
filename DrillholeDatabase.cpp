#include "DrillholeDatabase.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <algorithm>

// ── Yardımcı fonksiyonlar ─────────────────────────────────────────────────────

static std::string trimToken(std::string s) {
    const std::string ws = " \t\r\n";
    size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

static char detectDelimiter(const std::string& l) {
    for (char d : {';', ',', '\t'})
        if (l.find(d) != std::string::npos) return d;
    return ';';
}

static double safeParseGrade(const std::string& s) {
    if (s.empty()) return 0;
    try { return std::stod(s); } catch (...) { return 0; }
}

/*static*/ std::string DrillholeDatabase::normalizeId(const std::string& raw) {
    std::string s = raw;
    // UTF-8 BOM kaldır
    if (s.size() >= 3 &&
        (unsigned char)s[0] == 0xEF &&
        (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF) s.erase(0, 3);
    s.erase(std::remove_if(s.begin(), s.end(),
        [](unsigned char c){ return c < 0x20 || c == 0x7F; }), s.end());
    s = trimToken(s);
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c){ return (char)std::toupper(c); });
    return s;
}

void DrillholeDatabase::auditHoles() const {
    int ok = 0, nc = 0, ns = 0;
    for (const auto& [id, h] : _holes) {
        bool hc = !h.collar.holeId.empty(), hs = !h.samples.empty();
        if (hc && hs) { ++ok; }
        else {
            if (!hc) { ++nc; std::cout << "[DB] WARN no collar  -> \"" << id << "\"\n"; }
            if (!hs && hc) { ++ns; std::cout << "[DB] WARN no samples -> \"" << id << "\"\n"; }
        }
    }
    std::cout << "[DB] Audit: " << ok << " complete | "
              << nc << " no-collar | " << ns << " no-samples "
              << "(map=" << _holes.size() << ")\n";
}

std::vector<std::string> DrillholeDatabase::splitLine(const std::string& raw, char d) {
    std::string line = raw;
    line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
    std::vector<std::string> t;
    std::string tok;
    std::istringstream ts(line);
    while (std::getline(ts, tok, d)) t.push_back(trimToken(tok));
    return t;
}

static bool rewindIfData(std::ifstream& file, const std::string& path,
                          const std::string& first, char d, size_t idx) {
    std::string line = first;
    line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
    std::vector<std::string> t;
    std::string tok;
    std::istringstream ts(line);
    const std::string ws = " \t\r\n";
    while (std::getline(ts, tok, d)) {
        size_t s = tok.find_first_not_of(ws), e = tok.find_last_not_of(ws);
        t.push_back(s == std::string::npos ? "" : tok.substr(s, e - s + 1));
    }
    bool isData = false;
    if (t.size() > idx) { try { std::stod(t[idx]); isData = true; } catch (...) {} }
    if (isData) {
        file.close(); file.open(path);
        std::cout << "[DB] No header — re-reading from start.\n";
    }
    return isData;
}

// ── Yükleme fonksiyonları ─────────────────────────────────────────────────────

bool DrillholeDatabase::loadCollars(const std::string& fp) {
    std::ifstream f(fp);
    if (!f.is_open()) { std::cerr << "[DB] Cannot open: " << fp << "\n"; return false; }
    std::string first; std::getline(f, first);
    char d = detectDelimiter(first);
    std::cout << "[DB] Collars -- delim:'" << d << "'\n";
    rewindIfData(f, fp, first, d, 1);
    std::string line; int n = 0, sk = 0;
    while (std::getline(f, line)) {
        if (trimToken(line).empty()) continue;
        auto c = splitLine(line, d);
        if (c.size() < 5) { ++sk; continue; }
        try {
            std::string id = normalizeId(c[0]);
            if (id.empty()) { ++sk; continue; }
            _holes[id].collar = { id, std::stod(c[1]), std::stod(c[2]),
                                      std::stod(c[3]), std::stod(c[4]) };
            ++n;
        } catch (const std::exception& e) {
            if (sk < 10) std::cerr << "[DB] " << e.what() << "\n";
            else if (sk == 10) std::cerr << "[DB] suppressed.\n";
            ++sk;
        }
    }
    std::cout << "[DB] Collars: " << n << "  (sk:" << sk << ")\n";
    auditHoles();
    return n > 0;
}

bool DrillholeDatabase::loadSurveys(const std::string& fp) {
    std::ifstream f(fp);
    if (!f.is_open()) { std::cerr << "[DB] Cannot open: " << fp << "\n"; return false; }
    std::string first; std::getline(f, first);
    char d = detectDelimiter(first);
    std::cout << "[DB] Surveys -- delim:'" << d << "'\n";
    rewindIfData(f, fp, first, d, 1);
    std::string line; int n = 0, sk = 0;
    while (std::getline(f, line)) {
        if (trimToken(line).empty()) continue;
        auto c = splitLine(line, d);
        if (c.size() < 4) { ++sk; continue; }
        try {
            std::string id = normalizeId(c[0]);
            if (id.empty()) { ++sk; continue; }
            _holes[id].surveys.push_back({ std::stod(c[1]), std::stod(c[3]), std::stod(c[2]) });
            ++n;
        } catch (const std::exception& e) {
            if (sk < 10) std::cerr << "[DB] " << e.what() << "\n";
            else if (sk == 10) std::cerr << "[DB] suppressed.\n";
            ++sk;
        }
    }
    for (auto& [id, h] : _holes)
        std::sort(h.surveys.begin(), h.surveys.end(),
            [](const Survey& a, const Survey& b){ return a.depth < b.depth; });
    std::cout << "[DB] Surveys: " << n << "  (sk:" << sk << ")\n";
    auditHoles();
    return n > 0;
}

bool DrillholeDatabase::loadSamples(const std::string& fp) {
    std::ifstream f(fp);
    if (!f.is_open()) { std::cerr << "[DB] Cannot open: " << fp << "\n"; return false; }
    std::string first; std::getline(f, first);
    char d = detectDelimiter(first);
    std::cout << "[DB] Samples -- delim:'" << d << "'\n";
    rewindIfData(f, fp, first, d, 1);
    std::string line; int n = 0, sk = 0;
    while (std::getline(f, line)) {
        if (trimToken(line).empty()) continue;
        auto c = splitLine(line, d);
        if (c.size() < 4) { ++sk; continue; }
        try {
            std::string id = normalizeId(c[0]);
            if (id.empty()) { ++sk; continue; }
            double from, to, grade;
            if (c.size() >= 8) {
                from = std::stod(c[2]); to = std::stod(c[3]); grade = safeParseGrade(c[7]);
            } else if (c.size() == 5) {
                from = std::stod(c[2]); to = std::stod(c[3]); grade = safeParseGrade(c[4]);
            } else {
                from = std::stod(c[1]); to = std::stod(c[2]); grade = safeParseGrade(c[3]);
            }
            _holes[id].samples.push_back({ from, to, grade });
            ++n;
        } catch (const std::exception& e) {
            if (sk < 10) std::cerr << "[DB] " << e.what() << "\n";
            else if (sk == 10) std::cerr << "[DB] suppressed.\n";
            ++sk;
        }
    }
    std::cout << "[DB] Samples: " << n << "  (sk:" << sk << ")\n";
    auditHoles();
    return n > 0;
}


glm::vec3 DrillholeDatabase::computeCollarCentroid() const {
    if (_holes.empty()) return { 0, 0, 0 };
    double sx = 0, sy = 0; double mz = -1e30; int cnt = 0;
    for (const auto& [id, h] : _holes) {
        sx += h.collar.x; sy += h.collar.y;
        if (h.collar.z > mz) mz = h.collar.z;
        ++cnt;
    }
    return glm::vec3((float)(sx / cnt), (float)mz, (float)(-sy / cnt));
}

glm::vec3 DrillholeDatabase::dipAzToDir(double dp, double az) {
    double r = glm::pi<double>() / 180.0, dr = dp * r, ar = az * r;
    return glm::normalize(glm::vec3(
        (float)(cos(dr) * sin(ar)),
        (float)sin(dr),
        (float)(-cos(dr) * cos(ar))));
}

glm::vec3 DrillholeDatabase::interpolateAtDepth(const DrillPath& p, double d) {
    if (p.points.empty()) return { 0, 0, 0 };
    if (p.points.size() == 1) return p.points[0];
    for (size_t i = 0; i + 1 < p.depths.size(); ++i) {
        if ((float)d <= p.depths[i + 1]) {
            float r = p.depths[i + 1] - p.depths[i];
            if (r < 1e-6f) return p.points[i];
            return glm::mix(p.points[i], p.points[i + 1],
                            (float)(d - p.depths[i]) / r);
        }
    }
    return p.points.back();
}

DrillPath DrillholeDatabase::desurveySingleHole(const Drillhole& hole,
                                                  const glm::vec3& cen) const {
    DrillPath path;
    glm::vec3 col((float)hole.collar.x - cen.x,
                  (float)hole.collar.z - cen.y,
                  -(float)hole.collar.y - cen.z);
    path.points.push_back(col);
    path.depths.push_back(0.f);
    const auto& sv = hole.surveys;
    if (sv.empty()) {
        path.points.push_back(col + glm::vec3(0, -(float)hole.collar.maxDepth, 0));
        path.depths.push_back((float)hole.collar.maxDepth);
        return path;
    }
    glm::vec3 pos = col;
    for (size_t i = 0; i + 1 < sv.size(); ++i) {
        double seg = sv[i + 1].depth - sv[i].depth;
        if (seg <= 0) continue;
        glm::vec3 d1 = dipAzToDir(sv[i].dip, sv[i].azimuth);
        glm::vec3 d2 = dipAzToDir(sv[i + 1].dip, sv[i + 1].azimuth);
        float DL = std::acos(glm::clamp(glm::dot(d1, d2), -1.f, 1.f));
        float RF = DL < 1e-6f ? 1.f : (2.f / DL) * std::tan(DL * 0.5f);
        pos += (float)(seg * 0.5) * RF * (d1 + d2);
        path.points.push_back(pos);
        path.depths.push_back((float)sv[i + 1].depth);
    }
    double ld = sv.back().depth;
    if (ld < hole.collar.maxDepth - 1.0) {
        pos += dipAzToDir(sv.back().dip, sv.back().azimuth) *
               (float)(hole.collar.maxDepth - ld);
        path.points.push_back(pos);
        path.depths.push_back((float)hole.collar.maxDepth);
    }
    return path;
}

std::vector<WorldPoint> DrillholeDatabase::getDesurveyedSamples() const {
    std::vector<WorldPoint> pts;
    glm::vec3 cen = computeCollarCentroid();
    for (const auto& [id, h] : _holes) {
        if (h.samples.empty()) continue;
        DrillPath p = desurveySingleHole(h, cen);
        for (const auto& s : h.samples)
            pts.push_back({ interpolateAtDepth(p, (s.from + s.to) * 0.5), (float)s.grade });
    }
    std::cout << "[DB] Desurvey: " << pts.size() << " sample pts.\n";
    return pts;
}

std::vector<DrillPath> DrillholeDatabase::getDesurveyedPaths() const {
    std::vector<DrillPath> paths;
    glm::vec3 cen = computeCollarCentroid();
    for (const auto& [id, h] : _holes) {
        DrillPath p = desurveySingleHole(h, cen);
        for (const auto& s : h.samples)
            p.maxGrade = std::max(p.maxGrade, (float)s.grade);
        paths.push_back(std::move(p));
    }
    return paths;
}

// --- DrillholeDatabase.cpp dosyasının sonundaki fonksiyonu BUNUNLA değiştir ---

DrillholeDatabase::BoundingBox DrillholeDatabase::computeCollarBoundingBox() const {
    BoundingBox bb;
    bb.valid = false;

    // Tüm örnekleri (samples) alıp 3D dünya sınırlarını hesaplıyoruz
    auto samples = getDesurveyedSamples();
    if (samples.empty()) return bb;

    bb.minX = (double)samples[0].pos.x; bb.maxX = (double)samples[0].pos.x;
    bb.minY = (double)samples[0].pos.y; bb.maxY = (double)samples[0].pos.y;
    bb.minZ = (double)samples[0].pos.z; bb.maxZ = (double)samples[0].pos.z;

    for (const auto& s : samples) {
        if ((double)s.pos.x < bb.minX) bb.minX = (double)s.pos.x;
        if ((double)s.pos.x > bb.maxX) bb.maxX = (double)s.pos.x;
        if ((double)s.pos.y < bb.minY) bb.minY = (double)s.pos.y;
        if ((double)s.pos.y > bb.maxY) bb.maxY = (double)s.pos.y;
        if ((double)s.pos.z < bb.minZ) bb.minZ = (double)s.pos.z;
        if ((double)s.pos.z > bb.maxZ) bb.maxZ = (double)s.pos.z;
    }

    bb.valid = true;
    return bb;
}