# CMakeList.txt : CMake project for can_protocol_model, include source and define
# project specific logic here.
#

# Add source to this project's executable.
add_executable (
	can_protocol_model 
	"src/modules/config_registers.h" 
	"src/modules/message_ram.h" 
	"src/modules/can_types.h" 
	"src/modules/tx_handler.h" 
	"test/tb_tx_handler.cpp"
)

target_include_directories (can_protocol_model PRIVATE
    "${SYSTEMC_HOME}/include")

if(MSVC)
    target_compile_options(can_protocol_model PRIVATE /VMG)
endif()

target_link_libraries(can_protocol_model PRIVATE
    SystemC::systemc
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET can_protocol_model PROPERTY CXX_STANDARD 17)
endif()

# TODO: Add tests and install targets if needed.
