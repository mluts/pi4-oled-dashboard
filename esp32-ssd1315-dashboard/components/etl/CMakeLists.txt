# Embedded Template Library (https://github.com/ETLCPP/etl), header-only.
#
# NOTE: ETL's own CMakeLists.txt is a plain CMake project, not an IDF
# component, so it cannot be an idf_component.yml dependency: IDF includes every
# component's CMakeLists.txt in script mode first, where project() fails. This
# wrapper fetches the pinned tag during the real configure pass and exposes its
# include directory plus our etl_profile.h.
set(ETL_TAG "20.49.0")

if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register()
    return()
endif()

include(FetchContent)
# NOTE: the direct form of FetchContent_Populate only downloads; it never runs
# ETL's CMakeLists.txt. Lands in build/_deps/etl-src.
FetchContent_Populate(etl
    GIT_REPOSITORY https://github.com/ETLCPP/etl.git
    GIT_TAG ${ETL_TAG}
    GIT_SHALLOW TRUE
    GIT_PROGRESS FALSE
    QUIET
)

idf_component_register(INCLUDE_DIRS "${etl_SOURCE_DIR}/include" "profile")
