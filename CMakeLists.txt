cmake_minimum_required(VERSION 3.11.4)
project (treerex)

set(CMAKE_CXX_STANDARD 20)


if(NOT IPASIRSOLVER)
    set(IPASIRSOLVER "glucose4")
endif()
if(NOT IPASIRDIR)
    set(IPASIRDIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
endif()

# Build-specific compile options

set(MY_DEFINITIONS IPASIRSOLVER=\"${IPASIRSOLVER}\")
if(CMAKE_BUILD_TYPE MATCHES DEBUG)
    set(MY_DEFINITIONS ${MY_DEFINITIONS} TREEREX_VERSION=\"dbg-${IPASIRSOLVER}\")
    set(MY_DEBUG_OPTIONS "-rdynamic -g -ggdb") 
else()
    set(MY_DEFINITIONS ${MY_DEFINITIONS} TREEREX_VERSION=\"rls-${IPASIRSOLVER}\")
    set(MY_DEBUG_OPTIONS "-rdynamic -g -ggdb") 
    #set(BASE_COMPILEFLAGS -flto)
endif()


# Libraries and includes

link_directories(lib ${IPASIRDIR}/${IPASIRSOLVER} build)
set(BASE_LIBS ${MPI_CXX_LIBRARIES} ${MPI_CXX_LINK_FLAGS})
set(BASE_INCLUDES ${MPI_CXX_INCLUDE_PATH} src)
if(EXISTS ${IPASIRDIR}/${IPASIRSOLVER}/LIBS)
    message(STATUS "${IPASIRDIR}/${IPASIRSOLVER}/LIBS exists")
    file(READ ${IPASIRDIR}/${IPASIRSOLVER}/LIBS SOLVERLIBS)
else()
    message(STATUS "${IPASIRDIR}/${IPASIRSOLVER}/LIBS does not exist")
    # Assume that dummy functions are required
    set(SOLVERLIBS "-ldummyfuncs")
endif()


# If using UWrMaxSat with IPAMIR, update includes and libraries
if("${IPASIRSOLVER}" STREQUAL "uwrmaxsat")
    set(IPAMIRDIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    include_directories(${IPAMIRDIR})
    link_directories(${IPAMIRDIR})
    set(SOLVERLIBS "-luwrmaxsat")
    add_compile_definitions(USE_IPAMIR)
endif()

# Source files (without main.cpp)

set(BASE_SOURCES
    src/util/log.cpp src/util/params.cpp src/util/signal_manager.cpp src/util/timer.cpp src/util/project_utils.cpp src/util/command_utils.cpp src/util/names.cpp src/util/stacktrace.cpp src/util/dag_compressor.cpp
    src/data/htn_instance.cpp src/data/pdt_node.cpp src/data/mutex.cpp
    src/sat/encoding.cpp src/sat/variable_provider.cpp src/sat/bimander_amo.cpp
    src/algo/planner.cpp src/algo/plan_manager.cpp src/algo/effects_inference.cpp src/algo/effects_inference_scc.cpp src/algo/effects_inference_scc_certified.cpp
)


# Base library for Lilotane

add_library(lotane STATIC ${BASE_SOURCES})
target_include_directories(lotane PRIVATE ${BASE_INCLUDES})
target_compile_options(lotane PRIVATE ${BASE_COMPILEFLAGS})

add_compile_definitions("PROJECT_ROOT_DIR=${CMAKE_SOURCE_DIR}")

# Executable

add_executable(treerex src/main.cpp)
target_include_directories(treerex PRIVATE ${BASE_INCLUDES})
target_compile_options(treerex PRIVATE ${BASE_COMPILEFLAGS})
target_compile_definitions(treerex PRIVATE ${MY_DEFINITIONS})

if("${SOLVERLIBS}" MATCHES ".*dummyfuncs.*")
    # "dummyfuncs" library for unimplemented extra IPASIR functions
    add_library(dummyfuncs
        STATIC lib/dummyfuncs/dummyfuncs.cpp
    )
endif()
if("${IPASIRSOLVER}" STREQUAL "uwrmaxsat")
    target_link_libraries(treerex lotane ${BASE_LIBS} ${SOLVERLIBS})
else()
    if("${SOLVERLIBS}" MATCHES ".*[A-Za-z].*")
        target_link_libraries(treerex lotane ${BASE_LIBS} ipasir${IPASIRSOLVER} ${SOLVERLIBS})
    else()
        target_link_libraries(treerex lotane ${BASE_LIBS} ipasir${IPASIRSOLVER})
    endif()
endif()


# PandaPIparser
execute_process(
  COMMAND bash -c "cd ${CMAKE_SOURCE_DIR}/lib/parser && bash fetch_and_build_parser.sh"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# PandaPIgrounder
execute_process(
  COMMAND bash -c "cd ${CMAKE_SOURCE_DIR}/lib/grounder && bash fetch_and_build_grounder.sh"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# PandaPIplanner to be able to compute preconditions and effects of methods
execute_process(
  COMMAND bash -c "cd ${CMAKE_SOURCE_DIR}/lib/prec_and_effs_inference && bash fetch_and_build_prec_and_eff_inference.sh"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)




# Solver library

add_custom_target(solverlib cd .. && cd ${IPASIRDIR}/${IPASIRSOLVER}/ && [ ! -f fetch_and_build.sh ] || bash fetch_and_build.sh)
add_dependencies(treerex solverlib)


# Global debug flags

add_definitions("${MY_DEBUG_OPTIONS}")
SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${MY_DEBUG_OPTIONS}")



enable_testing()

# add_executable(test_arg_iterator src/test/test_arg_iterator.cpp)
# target_include_directories(test_arg_iterator PRIVATE ${BASE_INCLUDES})
# target_compile_options(test_arg_iterator PRIVATE ${BASE_COMPILEFLAGS})
# target_link_libraries(test_arg_iterator ${BASE_LIBS} lotane)
# add_test(NAME test_arg_iterator COMMAND test_arg_iterator)

