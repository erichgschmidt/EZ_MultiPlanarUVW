cmake_minimum_required(VERSION 3.20)

project(EZ_BoxTri LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point this to your local 3ds Max 2023 SDK folder.
# Pass from command line:
#   cmake -DMAXSDK_DIR="C:/Program Files/Autodesk/3ds Max 2023 SDK/maxsdk" ..
set(MAXSDK_DIR "C:/Program Files/Autodesk/3ds Max 2023 SDK/maxsdk"
    CACHE PATH "3ds Max SDK root folder")

add_library(EZ_BoxTri SHARED
    src/EZ_BoxTri.cpp
    src/EZ_AO.cpp
    src/EZ_Masks.cpp
    src/EZ_BoxTri.rc
)

target_include_directories(EZ_BoxTri PRIVATE
    "${MAXSDK_DIR}/include"
    src
)

target_link_directories(EZ_BoxTri PRIVATE
    "${MAXSDK_DIR}/lib/x64/Release"
)

target_link_libraries(EZ_BoxTri PRIVATE
    core.lib
    geom.lib
    mesh.lib
    maxutil.lib
    paramblk2.lib
)

target_compile_definitions(EZ_BoxTri PRIVATE
    UNICODE
    _UNICODE
    WIN32
    _WINDOWS
    NOMINMAX
)

target_compile_options(EZ_BoxTri PRIVATE
    /W3
    /EHsc
    /MD
)

set_target_properties(EZ_BoxTri PROPERTIES
    OUTPUT_NAME "EZ_BoxTri"
    SUFFIX ".dlm"
)
