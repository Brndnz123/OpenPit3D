// UI.cpp
#include "UI.h"
#include <imgui.h>
#include <cmath>
#include <string>
#include <mutex>

static const char* stageName(AppStage s) {
    switch(s){
        case AppStage::EMPTY:      return "No Data";
        case AppStage::DRILLHOLES: return "Drillholes Loaded";
        case AppStage::ESTIMATED:  return "Grades Estimated";
        case AppStage::OPTIMIZING: return "Working...";
        case AppStage::PIT_SHELL:  return "Pit Shell Ready";
    } return "Unknown";
}
static ImVec4 stageColor(AppStage s) {
    switch(s){
        case AppStage::EMPTY:      return {0.5f,0.5f,0.5f,1.f};
        case AppStage::DRILLHOLES: return {0.2f,0.7f,1.0f,1.f};
        case AppStage::ESTIMATED:  return {0.2f,1.0f,0.4f,1.f};
        case AppStage::OPTIMIZING: return {1.0f,0.8f,0.0f,1.f};
        case AppStage::PIT_SHELL:  return {1.0f,0.5f,0.1f,1.f};
    } return {1,1,1,1};
}

void UI::render(AppState& state, SceneController& scene,
                Renderer& renderer, Camera& /*camera*/)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos); ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  {0.f,0.f});
    ImGui::Begin("##DockHost",nullptr,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|
        ImGuiWindowFlags_NoResize  |ImGuiWindowFlags_NoMove    |
        ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoNavFocus|
        ImGuiWindowFlags_NoDocking |ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(3);
    ImGui::DockSpace(ImGui::GetID("MainDock"),{0,0},ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    drawToolbar      (state, scene);
    drawBlockModel   (state, scene);
    drawOptimisation (state, scene);
    drawScheduling   (state, scene);
    drawFinancials   (state);
    drawRenderPanel  (state, renderer, colorMode_);
    drawStatsOverlay (renderer, state);
    drawProgressModal(state, scene);
}

// ─────────────────────────────────────────────────────────────────────────────
void UI::drawToolbar(AppState& state, SceneController& scene) {
    ImGui::Begin("Toolbar");
    ImGui::TextColored(stageColor(state.stage),"● %s", stageName(state.stage));
    ImGui::SameLine(0,20);
    ImGui::TextDisabled("| Blocks: %d", state.grid.totalBlocks());

    ImGui::Separator();
    ImGui::Text("Import Drillhole Data (.txt only)");

    // File path inputs — .txt only
    auto txtInput = [](const char* label, char* buf, int size) {
        ImGui::SetNextItemWidth(185);
        ImGui::InputText(label, buf, size);
        // Inline warning if extension wrong
        std::string s(buf);
        bool ok = s.size() >= 4 && s.substr(s.size()-4) == ".txt";
        if (!s.empty() && !ok) {
            ImGui::SameLine();
            ImGui::TextColored({1.f,0.4f,0.4f,1.f},"⚠");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Only .txt files are accepted.");
        }
    };

    txtInput("Collars##c",  collarsPathBuf_, sizeof(collarsPathBuf_));
    ImGui::SameLine();
    txtInput("Surveys##sv", surveysPathBuf_, sizeof(surveysPathBuf_));
    ImGui::SameLine();
    txtInput("Samples##sm", samplesPathBuf_, sizeof(samplesPathBuf_));
    ImGui::SameLine();

    // Validate all three are .txt before enabling the button
    auto isTxt = [](const char* b) {
        std::string s(b);
        return s.size() >= 4 && s.substr(s.size()-4) == ".txt";
    };
    bool allTxt = isTxt(collarsPathBuf_) && isTxt(surveysPathBuf_) && isTxt(samplesPathBuf_);
    if (!allTxt) ImGui::BeginDisabled();
    if (ImGui::Button("Import Files")) {
        importStatus_ = "Importing...";
        bool ok = scene.importTextFiles(collarsPathBuf_, surveysPathBuf_, samplesPathBuf_);
        importStatus_ = ok
            ? "✓ Loaded " + std::to_string(
                  scene.data() ? (int)scene.data()->desurveyed().size() : 0)
              + " desurveyed samples"
            : "✗ Import failed — check file paths and format";
    }
    if (!allTxt) ImGui::EndDisabled();

    if (!importStatus_.empty()) {
        ImGui::SameLine(0, 16);
        bool success = !importStatus_.empty() && importStatus_[0] == 0xe2; // UTF-8 ✓
        // Simple check: starts with ✓ char or ✗
        ImVec4 col = (importStatus_.find("✓") != std::string::npos)
            ? ImVec4{0.3f,1.f,0.4f,1.f}
            : ImVec4{1.f,0.4f,0.4f,1.f};
        ImGui::TextColored(col, "%s", importStatus_.c_str());
    }

    ImGui::SameLine(0, 20);
    ImGui::TextDisabled("R = reset camera | Esc = quit");
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
void UI::drawBlockModel(AppState& state, SceneController& scene) {
    ImGui::Begin("Block Model / Kriging");

    if (ImGui::CollapsingHeader("Grid Setup", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int nx=50,ny=20,nz=50; static float bsz=20.f;
        ImGui::SliderInt("Blocks X",&nx,10,200); ImGui::SliderInt("Benches Y",&ny,5,80);
        ImGui::SliderInt("Blocks Z",&nz,10,200);
        ImGui::SliderFloat("Block size (m)",&bsz,5.f,50.f,"%.0f m");
        if (ImGui::Button("Allocate Grid")) {
            std::lock_guard<std::mutex> lk(state.gridMutex);
            state.grid.blockSizeX=bsz; state.grid.blockSizeY=bsz; state.grid.blockSizeZ=bsz;
            state.grid.allocate(nx,ny,nz);
            if (state.stage < AppStage::DRILLHOLES) state.stage=AppStage::DRILLHOLES;
        }
    }
    if (ImGui::CollapsingHeader("Variogram Model")) {
        if (varModel_.structures.empty()) varModel_.structures.push_back(VariogramStructure{});
        auto& st=varModel_.structures[0];
        float nugget=(float)varModel_.nugget; ImGui::SliderFloat("Nugget",&nugget,0.f,1.f); varModel_.nugget=nugget;
        float sill=(float)st.sill;  ImGui::SliderFloat("Sill",&sill,0.f,5.f); st.sill=sill;
        float rx=(float)st.rangeX,ry=(float)st.rangeY,rz=(float)st.rangeZ;
        ImGui::SliderFloat("Range X (m)",&rx,10.f,500.f); st.rangeX=rx;
        ImGui::SliderFloat("Range Y (m)",&ry,10.f,500.f); st.rangeY=ry;
        ImGui::SliderFloat("Range Z (m)",&rz,10.f,200.f); st.rangeZ=rz;
        float az=(float)st.azimuth,dip=(float)st.dip;
        ImGui::SliderFloat("Azimuth (°)",&az,0.f,360.f); st.azimuth=az;
        ImGui::SliderFloat("Dip (°)",&dip,0.f,90.f);     st.dip=dip;
    }
    if (ImGui::CollapsingHeader("Search Ellipsoid")) {
        float srx=(float)searchParams_.radiusX,sry=(float)searchParams_.radiusY,srz=(float)searchParams_.radiusZ;
        ImGui::SliderFloat("Search X",&srx,50.f,1000.f); searchParams_.radiusX=srx;
        ImGui::SliderFloat("Search Y",&sry,50.f,1000.f); searchParams_.radiusY=sry;
        ImGui::SliderFloat("Search Z",&srz,20.f,500.f);  searchParams_.radiusZ=srz;
        ImGui::SliderInt("Min samples",&searchParams_.minSamples,2,8);
        ImGui::SliderInt("Max samples",&searchParams_.maxSamples,8,32);
        ImGui::SliderInt("Max/octant", &searchParams_.maxPerOctant,1,6);
    }
    if (ImGui::CollapsingHeader("Block Kriging")) {
        ImGui::SliderInt("Disc. X",&krigingParams_.nx,2,8);
        ImGui::SliderInt("Disc. Y",&krigingParams_.ny,2,8);
        ImGui::SliderInt("Disc. Z",&krigingParams_.nz,2,8);
        ImGui::TextDisabled("Points/block: %d",krigingParams_.nx*krigingParams_.ny*krigingParams_.nz);
    }
    ImGui::Separator();
    bool kr=state.krigingState.running.load();
    if (!kr) {
        bool can=(state.stage>=AppStage::DRILLHOLES)&&!state.grid.blocks.empty();
        if (!can) ImGui::BeginDisabled();
        if (ImGui::Button("Run Kriging",{180,30})) {
            krigingParams_.blockSizeX=state.grid.blockSizeX;
            krigingParams_.blockSizeY=state.grid.blockSizeY;
            krigingParams_.blockSizeZ=state.grid.blockSizeZ;
            scene.startKriging(varModel_,searchParams_,krigingParams_);
        }
        if (!can) ImGui::EndDisabled();
    } else {
        ImGui::ProgressBar(state.krigingState.progress.load(),{180,20});
        ImGui::SameLine(); if(ImGui::Button("Cancel##k")) scene.cancelKriging();
    }
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
void UI::drawOptimisation(AppState& state, SceneController& scene) {
    ImGui::Begin("Pit Optimisation");
    if (ImGui::CollapsingHeader("Economics",ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Cut-off grade (%)",&pitParams_.cutOffGrade,0.1f,5.f);
        ImGui::SliderFloat("Ore value ($/t/%)",&pitParams_.oreValuePerTonne,10.f,200.f);
        ImGui::SliderFloat("Mining cost ($/t)",&pitParams_.miningCostPerTonne,2.f,30.f);
        ImGui::SliderFloat("Process cost ($/t)",&pitParams_.processingCostPerTonne,2.f,40.f);
        ImGui::SliderFloat("Block tonnes",&pitParams_.blockTonnes,1000.f,30000.f,"%.0f t");
    }
    if (ImGui::CollapsingHeader("Geotechnical"))
        ImGui::SliderFloat("Slope angle (°)",&pitParams_.slopeAngleDeg,25.f,75.f);

    ImGui::Separator();
    bool lr=state.lgState.running.load();
    if (!lr) {
        bool can=(state.stage>=AppStage::ESTIMATED);
        if (!can) ImGui::BeginDisabled();
        if (ImGui::Button("Run LG Optimisation",{210,30})) {
            state.optParams=pitParams_;
            scene.startLGOptimization(pitParams_);
        }
        if (!can) ImGui::EndDisabled();
    } else {
        ImGui::ProgressBar(state.lgState.progress.load(),{210,20});
        ImGui::SameLine(); if(ImGui::Button("Cancel##lg")) scene.cancelLG();
    }

    if (state.stage==AppStage::PIT_SHELL) {
        ImGui::Separator();
        ImGui::TextColored({0.4f,1.f,0.4f,1.f},"LG Results");
        std::lock_guard<std::mutex> lk(state.lgState.resultMutex);
        ImGui::Text("Pit value:  $%.2fM", state.lgState.totalPitValue/1e6);
        ImGui::Text("In pit:     %d blocks", state.lgState.blocksInPit);
        ImGui::Text("Strip ratio:%.2f:1",   state.lgState.strippingRatio);

        // Mesh generation status
        ImGui::Separator();
        if (state.meshState.running.load()) {
            ImGui::ProgressBar(state.meshState.progress.load(),{-1,16});
            ImGui::SameLine(); ImGui::TextDisabled("Generating meshes...");
        } else if (state.pitMesh.isUploaded()) {
            ImGui::TextColored({0.4f,1.f,0.4f,1.f},"✓ Pit mesh ready");
            ImGui::Text("  Pit faces:  %d", (int)state.pitMesh.indices.size()/3);
            ImGui::Text("  Topo faces: %d", (int)state.topoMesh.indices.size()/3);
        }
    }
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Scheduling panel — NEW
// ─────────────────────────────────────────────────────────────────────────────
void UI::drawScheduling(AppState& state, SceneController& scene) {
    ImGui::Begin("Mine Scheduling");

    if (ImGui::CollapsingHeader("Pushback Phases", ImGuiTreeNodeFlags_DefaultOpen)) {
        static float minRF   = 0.5f;
        static int   nSteps  = 6;
        ImGui::SliderFloat("Min revenue factor", &minRF,  0.1f, 0.9f);
        ImGui::SliderInt  ("Phase count",        &nSteps, 2,    10);
        ImGui::TextDisabled("Nested pits: revenue scaled from %.0f%% to 100%%",
                            minRF*100.f);

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Capacity Constraints")) {
            ImGui::SliderFloat("Mining capacity (Mt/yr)",
                               &schedParams_.maxMiningCapacityPerYear,
                               1e6f, 50e6f, "%.0f t");
            ImGui::SliderFloat("Milling capacity (Mt/yr)",
                               &schedParams_.maxMillingCapacityPerYear,
                               1e5f, 20e6f, "%.0f t");
        }

        ImGui::Separator();
        bool sr = state.schedState.running.load();
        if (!sr) {
            bool can = (state.stage >= AppStage::PIT_SHELL);
            if (!can) ImGui::BeginDisabled();
            if (ImGui::Button("Generate Schedule", {200,30})) {
                scene.startScheduling(state.optParams, schedParams_, minRF, nSteps);
            }
            if (!can) ImGui::EndDisabled();
        } else {
            ImGui::ProgressBar(state.schedState.progress.load(), {200,20});
        }
    }

    // ── Phase results table ───────────────────────────────────────────────────
    if (state.hasSchedule && !state.phases.empty()) {
        ImGui::Separator();
        ImGui::TextColored({0.4f,1.f,0.4f,1.f},"Phase Breakdown");
        ImGui::Columns(5,"phases");
        for (const char* h : {"Phase","RF","Total blks","Ore blks","Value $M"})
            { ImGui::Text("%s",h); ImGui::NextColumn(); }
        ImGui::Separator();
        for (const auto& p : state.phases) {
            ImGui::Text("%d",  p.phaseNumber);       ImGui::NextColumn();
            ImGui::Text("%.2f",p.revenueFactor);      ImGui::NextColumn();
            ImGui::Text("%d",  p.blocksTotal);         ImGui::NextColumn();
            ImGui::Text("%d",  p.oreBlocks);           ImGui::NextColumn();
            ImGui::Text("$%.1fM", p.totalValue/1e6f); ImGui::NextColumn();
        }
        ImGui::Columns(1);

        // Scheduled DCF results
        ImGui::Separator();
        ImGui::TextColored({0.4f,1.f,0.4f,1.f},"Scheduled DCF");
        double npv = state.scheduledCFA.npv();
        double irr = state.scheduledCFA.irr() * 100.0;
        double pb  = state.scheduledCFA.paybackYears();
        ImGui::TextColored(npv>=0?ImVec4{0.3f,1.f,0.3f,1.f}:ImVec4{1.f,0.3f,0.3f,1.f},
                           "NPV:     $%.2fM", npv/1e6);
        ImGui::Text("IRR:     %.1f%%", irr);
        if (std::isinf(pb)) ImGui::Text("Payback: Never");
        else                ImGui::Text("Payback: %.1f years", pb);

        if (ImGui::Button("Export Schedule CSV")) {
            state.scheduledCFA.exportCashFlowCSV("schedule_dcf.csv");
            ImGui::OpenPopup("SchSaved");
        }
        if (ImGui::BeginPopup("SchSaved"))
            { ImGui::Text("Saved to schedule_dcf.csv");
              if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
              ImGui::EndPopup(); }
    }
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
void UI::drawFinancials(AppState& state) {
    ImGui::Begin("Financials");
    if (ImGui::CollapsingHeader("Discount Settings",ImGuiTreeNodeFlags_DefaultOpen)) {
        float dr=(float)state.econParams.discountRate*100.f;
        if (ImGui::SliderFloat("Rate (%)",&dr,1.f,25.f))
            { state.econParams.discountRate=dr/100.f; cashFlowDirty_=true; }
        static bool midYear=true;
        if (ImGui::Checkbox("Mid-year convention",&midYear)) cashFlowDirty_=true;
        ImGui::SameLine(); ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("CF discounted at (t-0.5). Mining standard.");
    }
    if (ImGui::CollapsingHeader("CAPEX Schedule")) {
        static float capexAmounts[6]={50.f,80.f,30.f,0.f,0.f,0.f};
        static char  capexDescs[6][64]={"Pre-strip & site prep","Mill construction","Equipment commissioning","","",""};
        static int numCapex=3;
        for (int i=0;i<numCapex;++i) {
            ImGui::Text("Year %d",i); ImGui::SameLine(70);
            ImGui::SetNextItemWidth(80);
            if (ImGui::InputFloat(("##ca"+std::to_string(i)).c_str(),&capexAmounts[i],0,0,"%.1f")) cashFlowDirty_=true;
            ImGui::SameLine(); ImGui::SetNextItemWidth(180);
            if (ImGui::InputText(("##cd"+std::to_string(i)).c_str(),capexDescs[i],64)) cashFlowDirty_=true;
        }
        if (numCapex<6&&ImGui::SmallButton("+ Add")) ++numCapex;
        if (numCapex>1){ImGui::SameLine();if(ImGui::SmallButton("- Remove"))--numCapex;}
        if (cashFlowDirty_) {
            cashFlow_.clear(); cashFlow_.setDiscountRate(state.econParams.discountRate);
            for (int i=0;i<numCapex;++i) cashFlow_.addCapex(i,capexAmounts[i]*1e6,capexDescs[i]);
        }
    }
    if (ImGui::CollapsingHeader("Annual Operating Flows")) {
        static int   years=10; static float revM=120.f,opexM=55.f;
        ImGui::SliderInt("Life (yr)",&years,5,30);
        ImGui::SliderFloat("Revenue ($M/yr)",&revM,10.f,500.f);
        ImGui::SliderFloat("OPEX ($M/yr)",   &opexM,10.f,300.f);
        if (ImGui::Button("Apply##cfa")||cashFlowDirty_) {
            cashFlow_.setAnnualRevenues(std::vector<double>(years,revM*1e6));
            cashFlow_.setAnnualOpex    (std::vector<double>(years,opexM*1e6));
            cashFlow_.generate(); cashFlowDirty_=false;
        }
    }
    if (ImGui::CollapsingHeader("Results",ImGuiTreeNodeFlags_DefaultOpen)) {
        if (cashFlowDirty_) { cashFlow_.generate(); cashFlowDirty_=false; }
        double npv=cashFlow_.npv(), irr=cashFlow_.irr()*100.0, pb=cashFlow_.paybackYears();
        ImGui::TextColored(npv>=0?ImVec4{0.3f,1.f,0.3f,1.f}:ImVec4{1.f,0.3f,0.3f,1.f},
                           "NPV: $%.2fM", npv/1e6);
        ImGui::Text("IRR: %.1f%%",irr);
        if (std::isinf(pb)) ImGui::Text("Payback: Never");
        else                ImGui::Text("Payback: %.1f yr",pb);
        ImGui::Text("CAPEX:   $%.1fM",cashFlow_.totalCapex()/1e6);
        ImGui::Text("Revenue: $%.1fM",cashFlow_.totalRevenue()/1e6);
        ImGui::Text("OPEX:    $%.1fM",cashFlow_.totalOpex()/1e6);
        if (ImGui::TreeNode("DCF Table")) {
            ImGui::Columns(5,"dcf");
            for (const char* h:{"Year","Revenue","OPEX","FCF","Disc.FCF"})
                {ImGui::Text("%s",h);ImGui::NextColumn();}
            ImGui::Separator();
            for (const auto& r:cashFlow_.cashFlowTable()) {
                ImGui::Text("%d",r.year);ImGui::NextColumn();
                ImGui::Text("$%.1fM",r.revenue/1e6);ImGui::NextColumn();
                ImGui::Text("$%.1fM",r.opex/1e6);ImGui::NextColumn();
                ImGui::Text("$%.1fM",r.freeCashFlow/1e6);ImGui::NextColumn();
                ImGui::Text("$%.1fM",r.discountedFCF/1e6);ImGui::NextColumn();
            }
            ImGui::Columns(1); ImGui::TreePop();
        }
        ImGui::Separator();
        if (ImGui::Button("Export DCF"))         { cashFlow_.exportCashFlowCSV("dcf_output.csv"); ImGui::OpenPopup("Saved"); }
        ImGui::SameLine();
        if (ImGui::Button("Export Sensitivity")) { cashFlow_.exportSensitivityCSV("sensitivity.csv",1500.0,45.0); ImGui::OpenPopup("Saved"); }
        if (ImGui::BeginPopup("Saved")) { ImGui::Text("Saved."); if(ImGui::Button("OK"))ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }
    }
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
void UI::drawRenderPanel(AppState& state, Renderer& renderer, BlockColorMode& cm) {
    ImGui::Begin("Render Settings");
    const char* modes[]={"Flat","Grade","Economic"};
    int cur=(int)cm;
    if (ImGui::Combo("Color mode",&cur,modes,3)) cm=(BlockColorMode)cur;

    ImGui::Separator(); ImGui::Text("Block Model");
    ImGui::Checkbox("Show blocks",      &state.renderSettings.showBlocks);
    ImGui::Checkbox("Block wireframe",  &state.renderSettings.showWireframe);
    ImGui::Checkbox("Show drillholes",  &state.renderSettings.showDrillholes);

    ImGui::Separator(); ImGui::Text("Mesh Overlays");
    ImGui::Checkbox("Pit shell mesh",   &showPitMesh_);
    ImGui::Checkbox("Terrain mesh",     &showTopoMesh_);
    ImGui::Checkbox("Mesh wireframe",   &showMeshWire_);

    ImGui::Separator(); ImGui::Text("Lighting");
    ImGui::SliderFloat("Ambient",&state.renderSettings.ambientStr,0.f,1.f);
    ImGui::SliderFloat("Diffuse",&state.renderSettings.diffuseStr,0.f,1.f);

    renderer.setBlockSize(state.grid.blockSizeX);
    static glm::vec3 lightDir(1.f,2.f,1.5f);
    ImGui::SliderFloat3("Light dir",&lightDir.x,-3.f,3.f);
    renderer.setLightDir(lightDir);

    // Expose the mesh toggles to main.cpp via AppState's renderSettings
    // We reuse spare booleans: showGrid = pitMesh, showAxes = topoMesh
    state.renderSettings.showGrid = showPitMesh_;
    state.renderSettings.showAxes = showTopoMesh_;
    // Store wire flag in our own member; main.cpp reads it via renderSettings
    // (we piggyback into ambientStr sign — simpler: just expose directly)

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
void UI::drawStatsOverlay(const Renderer& renderer, const AppState& state) {
    ImGuiIO& io=ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x-230.f,10.f});
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##stats",nullptr,
        ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_AlwaysAutoResize|
        ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDocking);
    ImGui::Text("FPS:       %.0f",  io.Framerate);
    ImGui::Text("Drawn:     %d",    renderer.lastDrawnBlocks());
    ImGui::Text("Culled:    %d",    renderer.lastCulledBlocks());
    ImGui::Text("DrawCalls: %d",    renderer.lastDrawCalls());
    ImGui::Text("Pit mesh:  %s",    state.pitMesh.isUploaded()  ? "ready" : "–");
    ImGui::Text("Topo mesh: %s",    state.topoMesh.isUploaded() ? "ready" : "–");
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
void UI::drawProgressModal(AppState& state, SceneController& scene) {
    bool krig = state.krigingState.running.load();
    bool lg   = state.lgState.running.load();
    bool mesh = state.meshState.running.load();
    bool sched= state.schedState.running.load();
    if (!krig&&!lg&&!mesh&&!sched) return;

    ImGui::OpenPopup("Working...");
    ImVec2 c=ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c,ImGuiCond_Always,{0.5f,0.5f});
    ImGui::SetNextWindowSize({380,120});
    if (ImGui::BeginPopupModal("Working...",nullptr,
            ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings))
    {
        if (krig) {
            ImGui::Text("Kriging — estimating grades...");
            ImGui::ProgressBar(state.krigingState.progress.load(),{-1,0});
            ImGui::Text("Blocks estimated: %d",state.krigingState.blocksEstimated.load());
            if(ImGui::Button("Cancel##km")) scene.cancelKriging();
        } else if (lg) {
            ImGui::Text("LG Optimisation — computing pit shell...");
            ImGui::ProgressBar(state.lgState.progress.load(),{-1,0});
            if(ImGui::Button("Cancel##lgm")) scene.cancelLG();
        } else if (mesh) {
            ImGui::Text("Generating terrain meshes...");
            ImGui::ProgressBar(state.meshState.progress.load(),{-1,0});
        } else if (sched) {
            ImGui::Text("Generating mine schedule...");
            ImGui::ProgressBar(state.schedState.progress.load(),{-1,0});
        }
        ImGui::EndPopup();
    }
}
