cmake_minimum_required(VERSION 3.14)
project(TypeDeductionAnalysis)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON) 



# Find your installed LLVM
# find_package(LLVM REQUIRED CONFIG)
# find_package(LLVM 18.1.3 REQUIRED)
find_package(LLVM 18.1 REQUIRED)


message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

include(AddLLVM)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})


add_llvm_library(TypeDeductionAnalysis STATIC
  src/Containers/InsertionOrderedMap.hpp
  src/Debug/Logger.hpp
  src/Debug/Logger.cpp
  src/Utils/PrintUtils.hpp
  src/Utils/PtrCasts.hpp
  src/TDAInfo/DebugInfoParser.hpp
  src/TDAInfo/DebugInfoParser.cpp
  src/TDAInfo/StructPaddingInfo.hpp
  src/TDAInfo/StructPaddingInfo.cpp
  src/TDAInfo/TBAAParser.hpp
  src/TDAInfo/TBAAParser.cpp
  src/TDAInfo/TypeDeductionAnalysisInfo.hpp
  src/TDAInfo/TypeDeductionAnalysisInfo.cpp
  src/DeductionFunctions.cpp
  src/TransparentType.hpp
  src/TransparentType.cpp
  src/TypeDeductionAnalysis.hpp
  src/TypeDeductionAnalysis.cpp
)
target_include_directories(TypeDeductionAnalysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_property(TARGET TypeDeductionAnalysis PROPERTY POSITION_INDEPENDENT_CODE ON)

add_llvm_library(TypeDeductionAnalysisPlugin MODULE
  src/PassPluginInfo.cpp
  PLUGIN_TOOL opt
)
target_link_libraries(TypeDeductionAnalysisPlugin PRIVATE
  TypeDeductionAnalysis
)
