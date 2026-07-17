# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.10)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("can_protocol_model")

set(CMAKE_CXX_STANDARD 17)

# Find SystemC installation directory
list(APPEND CMAKE_PREFIX_PATH
    "D:/Software/systemc-3.0.2/systemc-3.0.2/install_msvc"
)
find_package(SystemCLanguage CONFIG REQUIRED)

# Include sub-projects.
add_subdirectory ("can_protocol_model")
