#pragma once
#include <string>
#include <vector>
#include <map>
#include <array>
#include <glm/glm.hpp>

// ── Temel sondaj veri yapıları ────────────────────────────────────────────────
struct Collar  { std::string holeId; double x, y, z, maxDepth; };
struct Survey  { double depth, azimuth, dip; };
struct Sample  { double from, to, grade; };

struct Drillhole {
    Collar              collar;
    std::vector<Survey> surveys;
    std::vector<Sample> samples;
};

struct WorldPoint { glm::vec3 pos; float grade; };
struct DrillPath  { std::vector<glm::vec3> points; std::vector<float> depths; float maxGrade = 0.f; };

// ── Yükleme ve çözümleme ──────────────────────────────────────────────────────
class DrillholeDatabase {
public:
    bool loadCollars (const std::string& filepath);
    bool loadSurveys (const std::string& filepath);
    bool loadSamples (const std::string& filepath);

    const std::map<std::string, Drillhole>& getHoles() const { return _holes; }

    // Desurvey
    std::vector<WorldPoint> getDesurveyedSamples() const;
    std::vector<DrillPath>  getDesurveyedPaths()   const;

    // ── Dinamik blok model boyutlandırması için ───────────────────────────────
    //  Örnek noktalarının 3D dünya koordinatlarındaki min/max sınırlarını döner.
    //  Boş veritabanında valid=false olur.
    struct BoundingBox {
        double minX =  1e30, minY =  1e30, minZ =  1e30;
        double maxX = -1e30, maxY = -1e30, maxZ = -1e30;
        bool valid = false;
    };

    // ── DÜZELTİLDİ: .cpp implementasyonuyla eşleşen tek imza ─────────────────
    //  Eski header'da iki farklı imza vardı (void + BoundingBox), bu derleme
    //  hatasına yol açıyordu.  Sadece BoundingBox döndüren versiyon kaldı.
    BoundingBox computeCollarBoundingBox() const;

private:
    std::map<std::string, Drillhole> _holes;

    glm::vec3 computeCollarCentroid() const;
    DrillPath desurveySingleHole(const Drillhole& hole, const glm::vec3& cen) const;

    static glm::vec3   dipAzToDir        (double dipDeg, double azDeg);
    static glm::vec3   interpolateAtDepth(const DrillPath& path, double depth);
    static std::string normalizeId       (const std::string& raw);
    void auditHoles() const;
    std::vector<std::string> splitLine(const std::string& line, char delimiter);
};
