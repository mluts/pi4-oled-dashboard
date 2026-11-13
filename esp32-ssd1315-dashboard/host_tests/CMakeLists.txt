# Builds the IDF-free modules and u8g2 for the Mac as one shared library for
# the pytest suite. conftest.py configures it into build/host_tests/.
cmake_minimum_required(VERSION 3.20)
project(dash_host C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clangd, see .clangd

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(U8G2 ${ROOT}/managed_components/u8g2/csrc)

# NOTE: managed_components/ is filled by the IDF component manager.
if(NOT EXISTS ${U8G2})
  message(FATAL_ERROR "${U8G2} is missing: run idf.py build first")
endif()

# NOTE: same tag as components/etl/CMakeLists.txt. Outside IDF's script mode
# ETL's own CMake project works as is and defines etl::etl.
include(FetchContent)
FetchContent_Declare(etl GIT_REPOSITORY https://github.com/ETLCPP/etl.git
                         GIT_TAG 20.49.0 GIT_SHALLOW ON)
FetchContent_MakeAvailable(etl)

# Vendored: only the graphics core (no mui* menus, no u8log* console), built
# once, warnings off.
file(GLOB U8G2_SRC ${U8G2}/u8g2_*.c ${U8G2}/u8x8_*.c)
add_library(u8g2 STATIC ${U8G2_SRC})
target_include_directories(u8g2 PUBLIC ${U8G2})
target_compile_options(u8g2 PRIVATE -w)
# Linked into a shared library; the macOS default already, not on Linux.
set_target_properties(u8g2 PROPERTIES POSITION_INDEPENDENT_CODE ON)

# What conftest.py loads with ctypes. No -Werror: the firmware build with GCC
# is the strict one, the Mac's clang warns about different things.
add_library(dash_host SHARED ${ROOT}/main/layout.cpp ${ROOT}/main/draw.cpp
                             shim.cpp)
target_compile_features(dash_host PRIVATE cxx_std_23)
target_compile_options(dash_host PRIVATE -Wall -Wextra)
target_include_directories(dash_host PRIVATE ${ROOT}/main
                                             ${ROOT}/components/etl/profile)
target_link_libraries(dash_host PRIVATE u8g2 etl::etl)
