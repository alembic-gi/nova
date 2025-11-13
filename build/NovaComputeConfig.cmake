
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was NovaComputeConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

# NovaCompute CMake Configuration File
# This file is used by find_package(Nova) to locate the Nova Compute Library

include(CMakeFindDependencyMacro)

# Find required dependencies
find_dependency(Vulkan REQUIRED)

# Include the targets file
include("${CMAKE_CURRENT_LIST_DIR}/NovaComputeTargets.cmake")

# Provide variables for compatibility
set(Nova_FOUND TRUE)
set(Nova_INCLUDE_DIRS "/nova")
set(Nova_LIBRARIES Nova::nova_compute)
set(Nova_VERSION "1.0.0")

check_required_components(Nova)
