cmake_minimum_required(VERSION 3.20)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(OpenPit3D VERSION 4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─────────────────────────────────────────────────────────────────────────────
#  Statik runtime — MSVC'de /MT kullan, böylece VC++ Redistributable gerekmez.
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W3 /utf-8)
    # Bazı eski Windows uyarılarını kapat
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wno-unused-variable -Wno-unused-parameter)
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  FetchContent — internet bağlantısı gerektiren tek seferlik indirme.
#  Sonraki buildlerde cmake cache'den kullanılır.
# ─────────────────────────────────────────────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ── 1. GLFW (statik, pencere/giriş) ──────────────────────────────────────────
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)  # GLFW'yu statik yap
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)

# ── 2. GLM (header-only matematik kütüphanesi) ────────────────────────────────
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        0.9.9.8
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# ── 3. GLEW (statik, OpenGL extension loader) ─────────────────────────────────
set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(glew-cmake_BUILD_STATIC ON  CACHE BOOL "" FORCE)
set(ONLY_LIBS               ON  CACHE BOOL "" FORCE)
FetchContent_Declare(glew
    GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
    GIT_TAG        glew-cmake-2.2.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glew)

# ── 4. Dear ImGui ─────────────────────────────────────────────────────────────
#  ImGui'nin kendi CMakeLists.txt'i yoktur;
#  kaynak dosyaları elle derliyoruz.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw)
# ImGui'nin kendi uyarılarını sustur
if(MSVC)
    target_compile_options(imgui_lib PRIVATE /W0)
else()
    target_compile_options(imgui_lib PRIVATE -w)
endif()

# ── 5. OpenMP (isteğe bağlı — IDW paralel hesaplama için) ────────────────────
find_package(OpenMP)

# ─────────────────────────────────────────────────────────────────────────────
#  Ana yürütülebilir dosya
# ─────────────────────────────────────────────────────────────────────────────
add_executable(OpenPit3D
    main.cpp
    BlockModel.cpp
    Camera.cpp
    DrillholeDatabase.cpp
    LGOptimizer.cpp
    MeshModel.cpp
    PitGenerator.cpp
    Renderer.cpp
    SceneController.cpp
    Shader.cpp
    UI.cpp
)

# Proje kökünü include path'e ekle (Types.h, AppState.h vb.)
target_include_directories(OpenPit3D PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ── Bağımlılıkları bağla ──────────────────────────────────────────────────────
target_link_libraries(OpenPit3D PRIVATE
    glfw
    libglew_static          # GLEW statik — GLEW_STATIC tanımını otomatik ekler
    glm
    imgui_lib
    $<$<BOOL:${OpenMP_CXX_FOUND}>:OpenMP::OpenMP_CXX>
)

# ── Platform OpenGL kütüphanesi ───────────────────────────────────────────────
if(WIN32)
    target_link_libraries(OpenPit3D PRIVATE opengl32)
elseif(APPLE)
    find_library(OPENGL_LIB OpenGL REQUIRED)
    target_link_libraries(OpenPit3D PRIVATE ${OPENGL_LIB})
elseif(UNIX)
    target_link_libraries(OpenPit3D PRIVATE GL)
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Build sonrası bilgi
# ─────────────────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "=== OpenPit3D Build Özeti ===")
message(STATUS "  Platform  : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler  : ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  OpenMP    : ${OpenMP_CXX_FOUND}")
message(STATUS "=============================")
message(STATUS "")