CMAKE_MINIMUM_REQUIRED(VERSION 3.16)
set(CXY_VERSION 0.1.0 CACHE STRING "The version of cxy")
string(REPLACE v "" ${CXY_VERSION} CXY_VERSION)
project(cxy
        VERSION ${CXY_VERSION}
        DESCRIPTION "Cxy is a transpiler for a high level programming language that tries to simplify C"
        LANGUAGES C CXX)

set(CXY_BUILD_ID 0 CACHE STRING "The build id of cxy")
# Configure path for loading project cmake scripts
set(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if (LLVM_ROOT_DIR)
    set(CMAKE_PREFIX_PATH
            ${CMAKE_PREFIX_PATH}
            ${LLVM_ROOT_DIR}/lib/cmake/llvm
            ${LLVM_ROOT_DIR}/lib/cmake/clang)
endif ()

find_package(Clang REQUIRED)
find_package(CURL REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (NOT CMAKE_BUILD_TYPE)
    # default build key is Debug
    set(CMAKE_BUILD_TYPE Debug)
endif ()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-c99-designator -Wno-reorder -fno-rtti")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

if (CMAKE_BUILD_TYPE MATCHES "asan")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer -g")
    set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
endif ()

option(ENABLE_TESTS "Enable CMake tests" ON)
option(ENABLE_UNIT_TESTS "Enable unit tests with Unity" ON)

include(CxyUtils)
include(FetchContent)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_subdirectory(src/3rdParty/msgpack)
add_subdirectory(src/3rdParty/libyaml)

include_directories(
        src/cxy
        src/3rdParty
)

add_executable(amalgamate
        src/tools/amalgamate.c
        src/cxy/core/utils.c
)

target_compile_definitions(amalgamate PRIVATE -DNO_EXEC_UTIL)

CxyAmalgamate(builtins
        OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        FILES
        src/cxy/runtime/builtins.cxy)

CxyAmalgamate(prologue
        OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        FILES
        src/cxy/runtime/prologue.h)

CxyAmalgamate(mm
        OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        FILES
        src/cxy/runtime/mm.h)

# Only added to the generated C in builds with memory trace
CxyAmalgamate(mm_trace
        OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        FILES
        src/cxy/runtime/mm_trace.h)

CxyAmalgamate(epilogue
        OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        FILES
        src/cxy/runtime/epilogue.h)

set(CXY_CORE_SOURCES
        src/cxy/core/args.c
        src/cxy/core/array.c
        src/cxy/core/completion.c
        src/cxy/core/e4c.c
        src/cxy/core/format.c
        src/cxy/core/hash.c
        src/cxy/core/hmap.c
        src/cxy/core/htable.c
        src/cxy/core/log.c
        src/cxy/core/mempool.c
        src/cxy/core/sb.c
        src/cxy/core/strpool.c
        src/cxy/core/synchronization.c
        src/cxy/core/utils.c)

set(CXY_PACKAGE_SOURCES
        src/cxy/package/types.c
        src/cxy/package/cxyfile.c
        src/cxy/package/validators.c
        src/cxy/package/gitops.c
        src/cxy/package/resolver.c
        src/cxy/package/env.c
        src/cxy/package/cache.c
        src/cxy/package/install_scripts.c
        src/cxy/package/registry.c
        src/cxy/package/commands/create.c
        src/cxy/package/commands/add.c
        src/cxy/package/commands/build.c
        src/cxy/package/commands/dispatch.c
        src/cxy/package/commands/install.c
        src/cxy/package/commands/update.c
        src/cxy/package/commands/test.c
        src/cxy/package/commands/run.c
        src/cxy/package/commands/remove.c
        src/cxy/package/commands/clean.c
        src/cxy/package/commands/info.c
        src/cxy/package/commands/find_system.c
        src/cxy/package/commands/login.c
        src/cxy/package/commands/publish.c
        src/cxy/package/commands/yank.c
        src/cxy/package/commands/list.c
        src/cxy/utils/async_tracker.c
        src/cxy/utils/commands/dispatch.c
        src/cxy/utils/commands/async_cmd.c
        src/cxy/utils/commands/wait_for.c
        src/cxy/utils/commands/lock.c
        src/cxy/utils/commands/mem.c)


set(CXY_3RD_PARTY_SOURCES
        src/3rdParty/cJSON.c
        src/3rdParty/toml/toml.c)

set(CXY_FRONTEND_SOURCES
        src/cxy/lang/frontend/ast.c
        src/cxy/lang/frontend/capture.c
        src/cxy/lang/frontend/encode.c
        src/cxy/lang/frontend/defines.c
        src/cxy/lang/frontend/decode.c
        src/cxy/lang/frontend/iterable.c
        src/cxy/lang/frontend/lexer.c
        src/cxy/lang/frontend/module.c
        src/cxy/lang/frontend/operator.c
        src/cxy/lang/frontend/strings.c
        src/cxy/lang/frontend/parser.c
        src/cxy/lang/frontend/strings.c
        src/cxy/lang/frontend/typegraph.c
        src/cxy/lang/frontend/ttable.c
        src/cxy/lang/frontend/types.c
        src/cxy/lang/frontend/visitor.c)

set(CXY_MIDDLE_SOURCES
        src/cxy/lang/middle/builtins.c
        src/cxy/lang/middle/defer.c
        src/cxy/lang/middle/mangle.c
        src/cxy/lang/middle/n2e.c

        src/cxy/lang/middle/sema/array.c
        src/cxy/lang/middle/sema/assign.c
        src/cxy/lang/middle/sema/binary.c
        src/cxy/lang/middle/sema/builtins.c
        src/cxy/lang/middle/sema/call.c
        src/cxy/lang/middle/sema/cast.c
        src/cxy/lang/middle/sema/check.c
        src/cxy/lang/middle/sema/class.c
        src/cxy/lang/middle/sema/closure.c
        src/cxy/lang/middle/sema/defaults.c
        src/cxy/lang/middle/sema/enum.c
        src/cxy/lang/middle/sema/exception.c
        src/cxy/lang/middle/sema/for.c
        src/cxy/lang/middle/sema/function.c
        src/cxy/lang/middle/sema/generics.c
        src/cxy/lang/middle/sema/if.c
        src/cxy/lang/middle/sema/index.c
        src/cxy/lang/middle/sema/inheritance.c
        src/cxy/lang/middle/sema/interface.c
        src/cxy/lang/middle/sema/match.c
        src/cxy/lang/middle/sema/member.c
        src/cxy/lang/middle/sema/new.c
        src/cxy/lang/middle/sema/node.c
        src/cxy/lang/middle/sema/path.c
        src/cxy/lang/middle/sema/struct.c
        src/cxy/lang/middle/sema/switch.c
        src/cxy/lang/middle/sema/tuple.c
        src/cxy/lang/middle/sema/type.c
        src/cxy/lang/middle/sema/unary.c
        src/cxy/lang/middle/sema/variables.c

        src/cxy/lang/middle/eval/macro.c
        src/cxy/lang/middle/eval/binary.c
        src/cxy/lang/middle/eval/comptime.c
        src/cxy/lang/middle/eval/enum.c
        src/cxy/lang/middle/eval/eval.c
        src/cxy/lang/middle/eval/for.c
        src/cxy/lang/middle/eval/index.c
        src/cxy/lang/middle/eval/literal.c
        src/cxy/lang/middle/eval/member.c
        src/cxy/lang/middle/eval/path.c
        src/cxy/lang/middle/eval/unary.c
        src/cxy/lang/middle/eval/xform.c

        src/cxy/lang/middle/bind/bind.c
        src/cxy/lang/middle/bind/discover.c
        src/cxy/lang/middle/bind/macro.c
        src/cxy/lang/middle/bind/resolve.c
        src/cxy/lang/middle/bind/scope.c

        src/cxy/lang/middle/dump/json.c
        src/cxy/lang/middle/dump/yaml.c
        src/cxy/lang/middle/dump/sexp.c
        src/cxy/lang/middle/dump/cxy.c

        src/cxy/lang/middle/shake/closure.c
        src/cxy/lang/middle/shake/shake.c

        src/cxy/lang/middle/preprocess/preprocessor.c
        src/cxy/lang/middle/preprocess/evaluate.c

        src/cxy/lang/middle/simplify/simplify.c

        src/cxy/lang/middle/ir/type.c
        src/cxy/lang/middle/ir/node.c
        src/cxy/lang/middle/ir/context.c
        src/cxy/lang/middle/ir/visitor.c
        src/cxy/lang/middle/ir/dump/dump.c
        src/cxy/lang/middle/ir/dump/sexp.c
        src/cxy/lang/middle/ir/lower/lower.c
        src/cxy/lang/middle/ir/lower/names.c
        src/cxy/lang/middle/ir/reachability/reachability.c
        src/cxy/lang/middle/memory/manage.c
        src/cxy/lang/middle/memory/finalize.c
        src/cxy/lang/middle/memory/verify.c
)

if (ENABLE_LLVM_BACKEND)
    set(CXY_LANG_BACKEND_SOURCES
            src/cxy/lang/backend/llvm/array.cpp
            src/cxy/lang/backend/llvm/binary.cpp
            src/cxy/lang/backend/llvm/context.cpp
            src/cxy/lang/backend/llvm/debug.cpp
            src/cxy/lang/backend/llvm/llvm.cpp
            src/cxy/lang/backend/llvm/generate.cpp
    )
else ()
    set(CXY_LANG_BACKEND_SOURCES
            src/cxy/lang/backend/c/generate.c
            src/cxy/lang/backend/c/typegraph.c
    )
endif ()

set(CXY_DRIVER_SOURCES
        src/cxy/driver/cc.c
        src/cxy/driver/cxyfile.c
        src/cxy/driver/driver.c
        src/cxy/driver/options.c
        src/cxy/driver/stages.c
        src/cxy/driver/stats.c
        src/cxy/driver/profiling.c
        src/cxy/driver/plugin.c

        src/cxy/driver/c-import/context.cpp
        src/cxy/driver/c-import/import.cpp
)

set(CXY_PLUGIN_LIB_SOURCES
        src/cxy/lang/frontend/ast.c
        src/cxy/lang/frontend/capture.c
        src/cxy/lang/frontend/operator.c
        src/cxy/lang/frontend/strings.c
        src/cxy/lang/frontend/ttable.c
        src/cxy/lang/frontend/types.c
        src/cxy/lang/frontend/lexer.c
        src/cxy/lang/frontend/parser.c
        src/cxy/lang/middle/builtins.c
        src/cxy/plugin/plugin.c
        src/cxy/plugin/shared.c
)

set(CXY_CORE_HEADERS
        src/cxy/core/alloc.h
        src/cxy/core/args.h
        src/cxy/core/array.h
        src/cxy/core/e4c.h
        src/cxy/core/format.h
        src/cxy/core/hash.h
        src/cxy/core/hmap.h
        src/cxy/core/htable.h
        src/cxy/core/log.h
        src/cxy/core/mempool.h
        src/cxy/core/primes.h
        src/cxy/core/sb.h
        src/cxy/core/strpool.h
        src/cxy/core/utils.h
)

set(CXY_PLUGIN_LIB_HEADERS
        src/cxy/lang/frontend/ast.h
        src/cxy/lang/frontend/flag.h
        src/cxy/lang/frontend/operator.h
        src/cxy/lang/frontend/strings.h
        src/cxy/lang/frontend/token.h
        src/cxy/lang/frontend/ttable.h
        src/cxy/lang/frontend/types.h
        src/cxy/plugin/plugin.h
)

# Archive members are stored by basename, so sources that share a name
# (eval/path.c and sema/path.c) end up as ambiguous members of a static library
# and the debug map of the executable loses their debug info. Those sources are
# compiled through uniquely named wrappers that #include the original file, the
# line tables still point at the original file. Generated sources are kept as is.
function(cxy_unique_object_names OUT)
    set(abs_sources)
    foreach (src IN LISTS ARGN)
        get_filename_component(abs "${src}" ABSOLUTE)
        list(APPEND abs_sources "${abs}")
    endforeach ()
    list(REMOVE_DUPLICATES abs_sources)

    set(names)
    set(duplicates)
    foreach (src IN LISTS abs_sources)
        get_filename_component(name "${src}" NAME)
        if (name IN_LIST names)
            list(APPEND duplicates "${name}")
        endif ()
        list(APPEND names "${name}")
    endforeach ()

    set(result)
    foreach (src IN LISTS abs_sources)
        get_filename_component(name "${src}" NAME)
        string(FIND "${src}" "${CMAKE_CURRENT_BINARY_DIR}" generated)
        if (name IN_LIST duplicates AND NOT generated EQUAL 0)
            file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}" "${src}")
            string(REPLACE "/" "_" unique "${rel}")
            set(wrapper "${CMAKE_CURRENT_BINARY_DIR}/unique/${unique}")
            file(GENERATE OUTPUT "${wrapper}" CONTENT "#include \"${src}\"\n")
            list(APPEND result "${wrapper}")
        else ()
            list(APPEND result "${src}")
        endif ()
    endforeach ()
    set(${OUT} "${result}" PARENT_SCOPE)
endfunction()

cxy_unique_object_names(CXY_CORE_ALL_SOURCES
        ${CXY_3RD_PARTY_SOURCES}
        ${CXY_CORE_SOURCES}
        ${CXY_PACKAGE_SOURCES}
        ${CXY_FRONTEND_SOURCES}
        ${CXY_MIDDLE_SOURCES}
        ${CXY_LANG_BACKEND_SOURCES}
        ${CXY_DRIVER_SOURCES}
        ${CMAKE_CURRENT_BINARY_DIR}/generated/src/builtins.c
        ${CMAKE_CURRENT_BINARY_DIR}/generated/src/prologue.c
        ${CMAKE_CURRENT_BINARY_DIR}/generated/src/mm.c
        ${CMAKE_CURRENT_BINARY_DIR}/generated/src/mm_trace.c
        ${CMAKE_CURRENT_BINARY_DIR}/generated/src/epilogue.c)

# Create static library with all core Cxy sources
add_library(cxy-core STATIC ${CXY_CORE_ALL_SOURCES})

add_executable(cxy
        src/cxy/driver/main.c
        src/cxy/plugin/shared.c)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/install/include/cxy/core)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/install/lib)

# The compiler as a library for tests and tools: compile in-process, keep the
# program, look nodes up and dump them (src/cxy/testing). Not part of the compiler
# binary.
add_library(cxy-testing STATIC
        src/cxy/testing/compiler.cpp
        src/cxy/testing/ast_find.c)
target_link_libraries(cxy-testing PUBLIC cxy-core)
target_include_directories(cxy-testing PUBLIC src/cxy src/3rdParty)
target_compile_definitions(cxy-testing PRIVATE
        -DCXY_TESTING_STDLIB_DIR="${CMAKE_SOURCE_DIR}/src/cxy")

add_library(cxy-trace STATIC
        src/cxy/stdlib/trace/coro.c
        src/cxy/stdlib/trace/mem.c)

add_custom_command(
        TARGET cxy-trace POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:cxy-trace>
        ${CMAKE_CURRENT_BINARY_DIR}/install/lib
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_library(cxy-plugin STATIC
        ${CXY_CORE_SOURCES}
        ${CXY_PLUGIN_LIB_SOURCES})
target_compile_definitions(cxy-plugin
        PUBLIC CXY_PLUGIN_LIB)

add_custom_command(
        TARGET cxy-plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CXY_PLUGIN_LIB_HEADERS}
        ${CMAKE_CURRENT_BINARY_DIR}/install/include/cxy
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CXY_CORE_HEADERS}
        ${CMAKE_CURRENT_BINARY_DIR}/install/include/cxy/core
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:cxy-plugin>
        ${CMAKE_CURRENT_BINARY_DIR}/install/lib
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

set(CLANG_LIBS clangAST clangBasic clangFrontend clangLex clangParse clangSema)
if (ENABLE_LLVM_BACKEND)
    target_compile_definitions(cxy PRIVATE "-DLLVM_BACKEND")
    # Find the libraries that correspond to the LLVM components
    # that we wish to use
    llvm_map_components_to_libnames(llvm_libs
            ${LLVM_TARGETS_TO_BUILD}
            Analysis
            Core
            Linker
            TransformUtils
            CodeGen
            Passes
            Support
    )
    target_link_libraries(cxy-core msgpack-c yaml ${llvm_libs} ${CLANG_LIBS} CURL::libcurl)
    target_link_libraries(cxy cxy-core)
else ()
    target_link_libraries(cxy-core msgpack-c yaml ${CLANG_LIBS} CURL::libcurl)
    target_link_libraries(cxy cxy-core)
endif ()

if (APPLE)
    target_link_options(cxy PRIVATE -Wl,-export_dynamic)
else ()
    target_link_options(cxy PRIVATE -Wl,--export-dynamic)
endif ()

target_compile_definitions(cxy-core PRIVATE
        -DCXY_VERSION="${CXY_VERSION}"
        -DCXY_BUILD_ID="${CXY_BUILD_ID}")

target_compile_definitions(cxy PRIVATE
        -DCXY_VERSION="${CXY_VERSION}"
        -DCXY_BUILD_ID="${CXY_BUILD_ID}")

set(CXY_STD_LIB_SOURCES
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/native
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/trace
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/args.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/atomic.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/base64.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/buffer.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/coro.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/crypto.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/dotenv.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/fetch.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/fserver.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/hash.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/http.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/json.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/jsonrpc.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/jwt.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/list.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/log.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/net.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/os.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/path.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/redis.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/sse.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/ssl.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/table.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/tcp.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/thread.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/time.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/trie.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/value.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/vector.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/ws.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/yaml.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/yaml/parser.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/yaml/serializer.cxy
)

set(CXY_STD_LIB_TEST_SOURCES
        ${CXY_STD_LIB_SOURCES}
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/json.spec.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/coro.spec.cxy
        ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/yaml/spec.cxy
)

set(CXY_STDLIB_PLUGINS
        jsonrpc.c
)

if (NOT TARGET FileCheck)
    # Download the LLVM FileCheck utility for tests.
    set(FILE_CHECK_SRC_PATH "llvmorg-${LLVM_PACKAGE_VERSION}/llvm/utils/FileCheck/FileCheck.cpp")
    set(FILE_CHECK_SRC_URL "https://raw.githubusercontent.com/llvm/llvm-project/${FILE_CHECK_SRC_PATH}")
    set(FILE_CHECK_SRC_FILE "${CMAKE_CURRENT_BINARY_DIR}/${FILE_CHECK_SRC_PATH}")
    if (NOT EXISTS "${FILE_CHECK_SRC_FILE}")
        message(STATUS "Downloading LLVM FileCheck source from ${FILE_CHECK_SRC_URL}")
        file(DOWNLOAD "${FILE_CHECK_SRC_URL}" "${FILE_CHECK_SRC_FILE}")
    endif ()
    add_executable(FileCheck ${FILE_CHECK_SRC_FILE})
    llvm_map_components_to_libnames(FILE_CHECK_NEEDED_LIBS filecheck support)
    target_link_libraries(FileCheck ${FILE_CHECK_NEEDED_LIBS})
endif ()

add_executable(lex-tester
        tests/utils/lexer.cpp
        src/cxy/lang/frontend/lexer.c
        ${CXY_CORE_SOURCES}
)

add_library(demo-plugin SHARED examples/plugin.c)
target_include_directories(
        demo-plugin
        PRIVATE ${CMAKE_BINARY_DIR}/install/include)
target_link_libraries(
        demo-plugin
        cxy-plugin
)

set_target_properties(demo-plugin
        PROPERTIES
        PREFIX ""
        SUFFIX ""
)

add_library(parser-plugin SHARED examples/parser-plugin.c)
target_include_directories(
        parser-plugin
        PRIVATE ${CMAKE_BINARY_DIR}/install/include)
target_link_libraries(
        parser-plugin
        cxy-plugin
)

set_target_properties(parser-plugin
        PROPERTIES
        PREFIX ""
        SUFFIX ""
)

# Add stdlib test cases
foreach (CXY_PLUGIN ${CXY_STDLIB_PLUGINS})
    get_filename_component(_PLUGIN ${CXY_PLUGIN} NAME_WE)
    message(STATUS "Adding cxy plugin target ${_PLUGIN} '${CXY_PLUGIN}'")
    add_library(${_PLUGIN} SHARED ${CMAKE_SOURCE_DIR}/src/cxy/stdlib/plugins/${CXY_PLUGIN})
    target_include_directories(
            ${_PLUGIN}
            PRIVATE ${CMAKE_BINARY_DIR}/install/include
    )
    target_link_libraries(
            ${_PLUGIN}
            cxy-plugin
    )
    set_target_properties(${_PLUGIN}
            PROPERTIES
            PREFIX ""
            SUFFIX ""
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/plugins
    )
    install(TARGETS ${_PLUGIN} DESTINATION lib/cxy/plugins)
endforeach ()

# Install cxy binary
install(TARGETS cxy DESTINATION bin)
# Install plugin library
install(TARGETS cxy-plugin DESTINATION lib)
# Install trace library
install(TARGETS cxy-trace DESTINATION lib)
# Install plugin headers
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/install/include/cxy
        DESTINATION include)
# Install stdlib
install(DIRECTORY src/cxy/stdlib DESTINATION lib/cxy/std)
# Install plugins
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/plugins DESTINATION lib/cxy)

include(CTest)
enable_testing()

# Add doctest C++ testing framework
if (ENABLE_UNIT_TESTS)
    # Helpers shared by test executables: AST and IR matching. Built once, on top
    # of cxy-testing, so a test only compiles its own sources.
    add_library(cxy-test STATIC
            tests/utils/ast.cpp
            tests/utils/ir.cpp
    )
    target_link_libraries(cxy-test PUBLIC cxy-testing cxy-core)
    target_include_directories(cxy-test PUBLIC
            src/cxy
            src/3rdParty
            tests/utils
            tests
    )
    target_compile_definitions(cxy-test PUBLIC
            -DTEST_ROOT_DIR="${CMAKE_SOURCE_DIR}"
    )

    # Create unit test executable
    add_executable(cxy_unit_tests
            tests/utils/test_ast.cpp
            tests/lang/middle/test_ir_sexp.cpp
            tests/lang/parser/test_literals.cpp
            tests/lang/parser/test_primitive_types.cpp
            tests/lang/parser/test_optional_parens.cpp
            tests/lang/parser/test_for_loop.cpp
            tests/lang/parser/test_at_copy.cpp
            tests/lang/parser/test_switch_statement.cpp
            tests/lang/parser/parser_utils.cpp
            tests/unit/test_utils.cpp
            tests/unit/test_warnings.cpp
            tests/lang/middle/test_ownership_verify.cpp
            tests/package/test_semver.cpp
            tests/package/test_version_constraints.cpp
            tests/package/test_cxyfile_parser.cpp
            tests/package/test_scripts.cpp
            tests/package/test_validators.cpp
            tests/package/test_gitops.cpp
            tests/package/test_resolver.cpp
            tests/package/test_cache.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/generated/src/builtins.c
            src/cxy/plugin/shared.c
    )

    target_link_libraries(cxy_unit_tests
            cxy-test
            cxy-core
    )

    target_include_directories(cxy_unit_tests PRIVATE
            src/cxy
            src/3rdParty
            tests/utils
            tests
    )

    target_compile_definitions(cxy_unit_tests PRIVATE
            -DCXY_VERSION="${CXY_VERSION}"
            -DCXY_BUILD_ID="${CXY_BUILD_ID}"
            -DTEST_ROOT_DIR="${CMAKE_SOURCE_DIR}"
    )

    # Add unit test to CTest
    add_test(NAME unit_tests COMMAND cxy_unit_tests)

    # Runtime memory manager tests, variants are listed in tests/runtime/variants.cfg
    add_test(NAME runtime_rc_tests
            COMMAND ${CMAKE_SOURCE_DIR}/tests/runtime/run.sh)
endif ()

# Add stdlib test cases
set(CXY_OPEN_SSL_OPTIONS "")
if (OPENSSL_ROOT_DIR)
    find_package(OpenSSL REQUIRED)
    set(CXY_OPEN_SSL_OPTIONS --c-header-dir ${OPENSSL_INCLUDE_DIR} --c-lib-dir ${OPENSSL_ROOT_DIR}/lib)
    message(STATUS "OpenSSL options needed for tests: ${CXY_OPEN_SSL_OPTIONS}")
endif ()

foreach (CXY_SRC ${CXY_STD_LIB_TEST_SOURCES})
    if (IS_DIRECTORY "${CXY_SRC}")
        continue()
    endif ()

    file(RELATIVE_PATH _FILE ${CMAKE_SOURCE_DIR}/src/cxy ${CXY_SRC})
    message(STATUS "Adding stdlib test case ${CXY_SRC}")

    # Each test compiles in its own directory: `app.c` and `app` are written to
    # the working directory, so tests sharing one would overwrite each other
    string(REPLACE "/" "_" _TEST_DIR_NAME "${_FILE}")
    set(_TEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/stdlib-tests/${_TEST_DIR_NAME})
    file(MAKE_DIRECTORY ${_TEST_DIR})
    add_test(NAME "${_FILE}"
            COMMAND cxy test ${CXY_OPEN_SSL_OPTIONS} --no-progress
            --stdlib ${CMAKE_SOURCE_DIR}/src/cxy
            --plugins-dir ${CMAKE_CURRENT_BINARY_DIR}/plugins ${CXY_SRC}
            WORKING_DIRECTORY ${_TEST_DIR})
endforeach ()

if (NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/plugins)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/plugins)
endif ()
