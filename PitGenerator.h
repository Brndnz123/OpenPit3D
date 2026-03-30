#pragma once
#include <vector>
#include <glm/glm.hpp>

class BlockModel;

struct RoadSegment { glm::vec3 startPos, endPos; float width; };
struct PitParams {
    int   benchCount = 12;
    float bottomRadius = 100.0f;
    float benchHeight = 15.0f;
    float bermWidth = 10.0f;
    float faceAngle = 70.0f;
    std::vector<RoadSegment> roadways;
};
struct Vertex { glm::vec3 position, normal, color; };
struct PitMeshData { std::vector<Vertex> vertices; std::vector<unsigned int> indices; };

class PitGenerator {
public:
    PitGenerator(unsigned int seed = 42);
    PitMeshData generateSurface(const PitParams& p, int resolution = 300);
    PitMeshData generateFromBlocks(const BlockModel& bm);
private:
    unsigned int _seed;
    float noise2D(float x, float y, int seed) const;
    float smoothNoise(float x, float y, int seed) const;
    float fractalNoise(float x, float y, int oct, int seed) const;
    float pointToSegmentDistance(glm::vec2 p, glm::vec2 a, glm::vec2 b, float& t) const;
    PitMeshData toFlatShaded(const PitMeshData& raw) const;
};