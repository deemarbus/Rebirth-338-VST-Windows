cmake_minimum_required(VERSION 3.20)
project(ReBirthIPCProtocolTests LANGUAGES C)

add_executable(rb-ipc-tests ipc-protocol-fixture.c)
set_target_properties(rb-ipc-tests PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO)
if(MSVC)
    target_compile_options(rb-ipc-tests PRIVATE /W4)
else()
    target_compile_options(rb-ipc-tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME rb-ipc-protocol COMMAND rb-ipc-tests)
set_tests_properties(rb-ipc-protocol PROPERTIES TIMEOUT 25)
