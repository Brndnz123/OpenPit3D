#pragma once
#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>
#include "Types.h"
#include "PitGenerator.h"

struct EconomicParams {
    float metalPrice = 100.f;  // $/t metal
    float recovery = 0.90f;  // 0–1
    float miningCost = 30.f;   // $/t rock
    float processCost = 20.f;   // $/t ore
    float sellingCost = 5.f;    // $/t metal
    float marketCap = 1e6f;   // limitleyici kapasite

    float computeCutoffGrade() const {
        float netSmelter = metalPrice * recovery - miningCost;
        if (netSmelter <= 0.f) return FLT_MAX;
        float g_m = (processCost + sellingCost) / netSmelter;
        float g_p = processCost / netSmelter;
        float g_k = (processCost + sellingCost) / (metalPrice * recovery);
        float gc = std::max({ std::min(g_m, g_p),
                               std::min(g_p, g_k),
                               std::min(g_m, g_k) });
        return std::max(0.f, gc);
    }
};

struct PitOptimizationParams {
    // ── Geoteknik Şev Bölgeleri (Bunu yanlışlıkla silmiştik, geri geldi) ──
    std::vector<SlopeRegion> slopeRegions = {
        {0.f, 50.f, 35.f, 35.f, 35.f, 35.f},
        {50.f, 1000.f, 45.f, 45.f, 45.f, 45.f}
    };

    EstimationMethod estimMethod = EstimationMethod::IDW;
    int              idwPower = 2;
    float            searchRadius = 0.f;
    float            rockDensity = 2.7f;

    // ── Kriging / Varyogram Parametreleri ──
    float            variogramNugget = 0.1f;    // C0 (Orijindeki sıçrama)
    float            variogramSill = 0.9f;    // C  (Kısmi Eşik)
    float            variogramRange = 100.0f;  // a  (Menzil - Etki Mesafesi)
    int              krigingMaxSample = 12;     // Bir blok için kullanılacak max örnek
};

struct AppEvents {
    bool runEstimation = false;
    bool runOptimization = false;
    bool exportReport = false;
    bool saveProject = false;
    bool loadProject = false;
    std::string projectPath;
};

struct AppState {
    double lastX = 0, lastY = 0;
    bool   leftDown = false;
    bool   rightDown = false;

    AppStage   stage = AppStage::EMPTY;
    ViewPreset view = ViewPreset::FREE;

    PitParams             params;
    EconomicParams        econ;
    PitOptimizationParams opt;
    RenderSettings        render;
    AppEvents             events;

    bool animating = false;
    bool needRegen = false;
};