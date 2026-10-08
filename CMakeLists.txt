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
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent Network)
endif()

add_library(omnidrop_core
  src/domain/file_kind.cpp
  src/app/action_catalog.cpp
  src/app/activation_protocol.cpp
  src/app/diagnostics.cpp
  src/app/omnidrop_service.cpp
)

target_include_directories(omnidrop_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(omnidrop_core PUBLIC Qt6::Core)
target_compile_definitions(omnidrop_core PUBLIC OMNIDROP_VERSION="${PROJECT_VERSION}")

add_library(omnidrop_adapters
  src/adapters/diagnostics_service.cpp
  src/adapters/python_worker_client.cpp
  src/app/translation_service.cpp
  src/app/workflow_service.cpp
  src/app/workflow_job_service.cpp
  src/adapters/recent_files_store.cpp
  src/adapters/action_history_store.cpp
)
target_include_directories(omnidrop_adapters PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(omnidrop_adapters PUBLIC omnidrop_core Qt6::Core)

if(OMNIDROP_BUILD_GUI)
  add_library(omnidrop_desktop
    src/platform/single_instance_coordinator.cpp
    src/platform/single_instance_coordinator.hpp
  )
  target_include_directories(omnidrop_desktop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(omnidrop_desktop PUBLIC omnidrop_core Qt6::Core Qt6::Network)

  set(OMNIDROP_GUI_SOURCES
    src/gui/main.cpp
    src/gui/main_window.cpp
    src/gui/translation_dialog.cpp
    src/gui/workflow_dialog.cpp
    src/gui/workflow_graph_view.cpp
    src/gui/workflow_graph_view.hpp
    src/gui/drop_zone.cpp
    assets/resources.qrc
  )

  if(WIN32)
    set(OMNIDROP_ICON_RC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/windows/omnidrop.ico")
    configure_file(
      assets/windows/omnidrop.rc.in
      "${CMAKE_CURRENT_BINARY_DIR}/omnidrop.rc"
      @ONLY
    )
    list(APPEND OMNIDROP_GUI_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/omnidrop.rc")
  endif()

  add_executable(omnidrop WIN32 ${OMNIDROP_GUI_SOURCES})
  target_link_libraries(omnidrop PRIVATE
    omnidrop_core
    omnidrop_adapters
    omnidrop_desktop
    Qt6::Widgets
    Qt6::Concurrent
  )
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

  add_executable(omnidrop_activation_protocol_tests tests/activation_protocol_tests.cpp)
  target_link_libraries(omnidrop_activation_protocol_tests PRIVATE omnidrop_core Qt6::Core)
  add_test(NAME omnidrop_activation_protocol_tests COMMAND omnidrop_activation_protocol_tests)

  add_executable(omnidrop_diagnostics_tests tests/diagnostics_tests.cpp)
  target_link_libraries(omnidrop_diagnostics_tests PRIVATE omnidrop_core Qt6::Core)
  add_test(NAME omnidrop_diagnostics_tests COMMAND omnidrop_diagnostics_tests)

  add_executable(omnidrop_translation_service_tests tests/translation_service_tests.cpp)
  target_link_libraries(omnidrop_translation_service_tests PRIVATE omnidrop_adapters Qt6::Core)
  add_test(NAME omnidrop_translation_service_tests COMMAND omnidrop_translation_service_tests)

  add_executable(omnidrop_workflow_service_tests tests/workflow_service_tests.cpp)
  target_link_libraries(omnidrop_workflow_service_tests PRIVATE omnidrop_adapters Qt6::Core)
  add_test(NAME omnidrop_workflow_service_tests COMMAND omnidrop_workflow_service_tests)

  add_executable(omnidrop_workflow_job_tests tests/workflow_job_service_tests.cpp)
  target_link_libraries(omnidrop_workflow_job_tests PRIVATE omnidrop_adapters Qt6::Core)
  add_test(NAME omnidrop_workflow_job_tests COMMAND omnidrop_workflow_job_tests)

  if(TARGET omnidrop_cli)
    add_test(NAME omnidrop_cli_version COMMAND omnidrop_cli --version)
    set_tests_properties(omnidrop_cli_version PROPERTIES
      PASS_REGULAR_EXPRESSION "^${PROJECT_VERSION}"
    )

    add_test(NAME omnidrop_cli_workflow_jobs COMMAND omnidrop_cli workflow jobs)
    set_tests_properties(omnidrop_cli_workflow_jobs PROPERTIES
      PASS_REGULAR_EXPRESSION "schema_version"
      ENVIRONMENT "OMNIDROP_WORKFLOW_JOBS=${CMAKE_CURRENT_BINARY_DIR}/ci-jobs/jobs.json"
    )

    add_test(NAME omnidrop_cli_workflow_validate
             COMMAND omnidrop_cli workflow validate
                     "${CMAKE_CURRENT_SOURCE_DIR}/workflows/examples/text-clean.omniworkflow.json")
    set_tests_properties(omnidrop_cli_workflow_validate PROPERTIES
      PASS_REGULAR_EXPRESSION "node_count"
    )

    add_test(NAME omnidrop_cli_diagnostics COMMAND omnidrop_cli diagnostics)
    set_tests_properties(omnidrop_cli_diagnostics PROPERTIES
      PASS_REGULAR_EXPRESSION "\"schema_version\""
    )
  endif()

  if(OMNIDROP_BUILD_GUI)
    add_executable(omnidrop_graph_view_tests
      tests/workflow_graph_view_tests.cpp
      src/gui/workflow_graph_view.cpp
      src/gui/workflow_graph_view.hpp)
    target_link_libraries(omnidrop_graph_view_tests PRIVATE omnidrop_core Qt6::Widgets)
    add_test(NAME omnidrop_graph_view_tests COMMAND omnidrop_graph_view_tests)
    set_tests_properties(omnidrop_graph_view_tests PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()

  if(TARGET omnidrop_desktop)
    add_executable(omnidrop_single_instance_tests tests/single_instance_tests.cpp)
    target_link_libraries(omnidrop_single_instance_tests PRIVATE omnidrop_desktop Qt6::Core)
    add_test(NAME omnidrop_single_instance_tests COMMAND omnidrop_single_instance_tests)
  endif()

  if(WIN32 AND TARGET omnidrop)
    add_test(
      NAME omnidrop_windows_identity
      COMMAND powershell -NoProfile -ExecutionPolicy Bypass
              -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_windows_identity.ps1"
              -Executable "$<TARGET_FILE:omnidrop>"
              -ExpectedVersion "${PROJECT_VERSION}"
    )
  endif()
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
install(DIRECTORY workflows DESTINATION .)
install(FILES README.md LICENSE DESTINATION .)

set(CPACK_PACKAGE_NAME "OmniDrop")
set(CPACK_PACKAGE_VENDOR "OmniDrop")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "OmniDrop-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "ZIP")
include(CPack)
