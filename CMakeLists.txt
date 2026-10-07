cmake_minimum_required(VERSION 3.24)

project(OmniDrop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(OMNIDROP_BUILD_GUI "Build the Qt desktop application" ON)
option(OMNIDROP_BUILD_CLI "Build the command-line application" ON)
option(OMNIDROP_BUILD_TESTS "Build tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core)
if(OMNIDROP_BUILD_GUI)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent)
endif()

add_library(omnidrop_core
  src/domain/file_kind.cpp
  src/app/action_catalog.cpp
  src/app/omnidrop_service.cpp
)

target_include_directories(omnidrop_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(omnidrop_core PUBLIC Qt6::Core)
target_compile_definitions(omnidrop_core PUBLIC OMNIDROP_VERSION="${PROJECT_VERSION}")

add_library(omnidrop_adapters
  src/adapters/python_worker_client.cpp
  src/adapters/recent_files_store.cpp
  src/adapters/action_history_store.cpp
)
target_include_directories(omnidrop_adapters PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(omnidrop_adapters PUBLIC omnidrop_core Qt6::Core)

if(OMNIDROP_BUILD_GUI)
  add_executable(omnidrop WIN32
    src/gui/main.cpp
    src/gui/main_window.cpp
    src/gui/drop_zone.cpp
  )
  target_link_libraries(omnidrop PRIVATE omnidrop_core omnidrop_adapters Qt6::Widgets Qt6::Concurrent)
  set_target_properties(omnidrop PROPERTIES OUTPUT_NAME "OmniDrop")
endif()

if(OMNIDROP_BUILD_CLI)
  add_executable(omnidrop_cli src/cli/main.cpp)
  target_link_libraries(omnidrop_cli PRIVATE omnidrop_core omnidrop_adapters Qt6::Core)
  set_target_properties(omnidrop_cli PROPERTIES OUTPUT_NAME "omnidrop-cli")
endif()

include(CTest)
if(OMNIDROP_BUILD_TESTS AND BUILD_TESTING)
  add_executable(omnidrop_core_tests tests/action_catalog_tests.cpp)
  target_link_libraries(omnidrop_core_tests PRIVATE omnidrop_core Qt6::Core)
  add_test(NAME omnidrop_core_tests COMMAND omnidrop_core_tests)
endif()

# Install layout for development/portable packaging. A self-contained Python runtime is a later
# distribution concern; the worker and its dependency manifest are installed beside the binaries.
if(TARGET omnidrop)
  install(TARGETS omnidrop RUNTIME DESTINATION .)
endif()
if(TARGET omnidrop_cli)
  install(TARGETS omnidrop_cli RUNTIME DESTINATION .)
endif()
install(DIRECTORY python DESTINATION .
        PATTERN "__pycache__" EXCLUDE
        PATTERN "*.pyc" EXCLUDE)
install(DIRECTORY assets DESTINATION .)
install(FILES README.md LICENSE DESTINATION .)

set(CPACK_PACKAGE_NAME "OmniDrop")
set(CPACK_PACKAGE_VENDOR "OmniDrop")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "OmniDrop-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "ZIP")
include(CPack)
