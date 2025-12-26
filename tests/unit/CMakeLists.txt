# Unit Tests CMakeLists.txt
# Add this to the main CMakeLists.txt with: add_subdirectory(tests/unit)

# Find all test source files
file(GLOB TEST_SOURCES "*.cpp")

# Create test executables
foreach(TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_features(${TEST_NAME} PRIVATE cxx_std_17)

    # Link any required libraries (add as needed)
    # target_link_libraries(${TEST_NAME} PRIVATE your_library)

    # Register as test
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# Custom target to run all unit tests
add_custom_target(run_unit_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS ${TEST_SOURCES}
    COMMENT "Running all unit tests"
)