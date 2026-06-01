cmake_minimum_required(VERSION 3.20)

project(EZMultiPlanarUVW LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point this to your local 3ds Max 2023 SDK folder.
# Pass from command line:
#   cmake -DMAXSDK_DIR="C:/Program Files/Autodesk/3ds Max 2023 SDK/maxsdk" ..
set(MAXSDK_DIR "C:/Program Files/Autodesk/3ds Max 2023 SDK/maxsdk"
    CACHE PATH "3ds Max SDK root folder")

add_library(EZMultiPlanarUVW SHARED
    src/EZMultiPlanarUVW.cpp
    src/EZMultiPlanarUVW.rc
)

target_include_directories(EZMultiPlanarUVW PRIVATE
    "${MAXSDK_DIR}/include"
    src
)

target_link_directories(EZMultiPlanarUVW PRIVATE
    "${MAXSDK_DIR}/lib/x64/Release"
)

target_link_libraries(EZMultiPlanarUVW PRIVATE
    core.lib
    geom.lib
    mesh.lib
    maxutil.lib
    paramblk2.lib
)

target_compile_definitions(EZMultiPlanarUVW PRIVATE
    UNICODE
    _UNICODE
    WIN32
    _WINDOWS
    NOMINMAX
)

target_compile_options(EZMultiPlanarUVW PRIVATE
    /W3
    /EHsc
    /MD
)

set_target_properties(EZMultiPlanarUVW PROPERTIES
    OUTPUT_NAME "EZMultiPlanarUVW"
    SUFFIX ".dlm"
)
