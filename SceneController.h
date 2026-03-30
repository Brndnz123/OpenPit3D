#pragma once
#include <thread>
#include <atomic>
#include <future>
#include <chrono>
#include "Types.h"
#include "AppState.h"

class Renderer; class Camera; class PitGenerator;
class BlockModel; class DrillholeDatabase; class LGOptimizer;

// ── SceneController — Event-Driven Mimari ────────────────────────────────────
//
//  Eski doğrusal SPACE-machine kaldırıldı.  Artık GUI (ileride ImGui)
//  doğrudan aşağıdaki public metodları çağırır; ya da AppEvents bayraklarını
//  set eder ve tick() onları işler.
//
//  Akış:
//    1. main.cpp → setup()
//    2. Dosya sürüklendi → onDataLoaded()   (collar + sample yüklenince)
//    3. GUI "Kestir" butonu → AppEvents::runEstimation = true
//    4. GUI "Optimize Et" butonu → AppEvents::runOptimization = true
//    5. GUI "Animasyon Başlat" → state.animating = true
//    6. tick() her frame çağrılır; async sonuçları ve animasyonu yönetir.

class SceneController {
public:
    ~SceneController();

    void setup(Renderer* r, Camera* c, PitGenerator* g,
               BlockModel* bm, DrillholeDatabase* db, LGOptimizer* opt);

    // Collar + sample yüklendikten sonra bir kez çağrılır.
    // Bloğun dinamik boyutunu hesaplar ve ilk sondaj görüntüsünü oluşturur.
    void onDataLoaded(AppState& state);

    // Ekonomik parametreler değiştiğinde kestirim yeniden çalıştırılır.
    void runEstimation(AppState& state);

    // Arka planda LG thread başlatır.
    void runOptimization(AppState& state);

    // Her frame main loop tarafından çağrılır.
    // AppEvents bayraklarını tüketir, async sonuçları işler, animasyonu ilerletir.
    // Görsel güncellenme gerektiren bir şey olduysa true döner.
    bool tick(AppState& state);

    // Sorgular
    AppStage currentStage()  const { return _stage; }
    bool     dataReady()     const { return _dataReady; }
    bool     lgRunning()     const { return _lgRunning.load(); }
    bool     isSimComplete() const { return _simComplete; }

private:
    void transitionTo(AppStage s, AppState& state);
    void launchLGThread(AppState& state);
    void finalizeLG(AppState& state);
    void rebuildTerrain();
    bool advanceBench();

    Renderer*          _renderer = nullptr;
    Camera*            _camera   = nullptr;
    PitGenerator*      _gen      = nullptr;
    BlockModel*        _bm       = nullptr;
    DrillholeDatabase* _db       = nullptr;
    LGOptimizer*       _opt      = nullptr;

    AppStage _stage     = AppStage::EMPTY;
    bool     _dataReady = false;

    std::thread       _lgThread;
    std::atomic<bool> _lgRunning{false};
    std::atomic<bool> _lgDone{false};

    int  _currentBench = 0;
    bool _simComplete  = false;
    std::chrono::steady_clock::time_point _lastBenchTime;
    static constexpr float BENCH_INTERVAL_SEC = 0.8f;
};
