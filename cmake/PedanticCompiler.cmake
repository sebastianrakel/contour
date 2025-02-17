include(CheckCXXCompilerFlag)
function(try_add_compile_options FLAG)
    # Remove leading - or / from the flag name.
    string(REGEX REPLACE "^[-/]" "" name ${FLAG})
    # Deletes any ':' because it's invalid variable names.
    string(REGEX REPLACE ":" "" name ${name})
    check_cxx_compiler_flag(${FLAG} ${name})
    if(${name})
        message(STATUS "Adding compiler flag: ${FLAG}.")
        add_compile_options(${FLAG})
    else()
        message(STATUS "Adding compiler flag: ${FLAG} failed.")
    endif()

    # If the optional argument passed, store the result there.
    if(ARGV1)
        set(${ARGV1} ${name} PARENT_SCOPE)
    endif()
endfunction()

option(PEDANTIC_COMPILER "Compile the project with almost all warnings turned on." OFF)
option(PEDANTIC_COMPILER_WERROR "Enables -Werror to force warnings to be treated as errors." OFF)

# Always show diagnostics in colored output.
try_add_compile_options(-fdiagnostics-color=always)

if(${PEDANTIC_COMPILER})
    if(("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU") OR ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang"))
        message(STATUS "Enabling pedantic compiler options: yes")
        # TODO: check https://github.com/lefticus/cppbestpractices/blob/master/02-Use_the_Tools_Available.md#compilers
        try_add_compile_options(-Qunused-arguments)
        try_add_compile_options(-Wall)
        try_add_compile_options(-Wconversion)
        try_add_compile_options(-Wduplicate-enum)
        try_add_compile_options(-Wduplicated-cond)
        try_add_compile_options(-Wextra)
        try_add_compile_options(-Wextra-semi)
        try_add_compile_options(-Wfinal-dtor-non-final-class)
        try_add_compile_options(-Wimplicit-fallthrough)
        try_add_compile_options(-Wlogical-op)
        try_add_compile_options(-Wmissing-declarations)
        try_add_compile_options(-Wnewline-eof)
        try_add_compile_options(-Wno-unknown-attributes)
        try_add_compile_options(-Wno-unknown-pragmas)
        try_add_compile_options(-Wnull-dereference)
        try_add_compile_options(-Wpessimizing-move)
        try_add_compile_options(-Wredundant-move)
        try_add_compile_options(-Wsign-conversion)
        try_add_compile_options(-Wsuggest-destructor-override)
        try_add_compile_options(-pedantic)
    else()
        message(STATUS "Enabling pedantic compiler options: unsupported platform")
    endif()
else()
    message(STATUS "Enabling pedantic compiler options: no")
endif()

if(${PEDANTIC_COMPILER_WERROR})
    try_add_compile_options(-Werror) # XXX Not yet, but hopefully soon.

    # Not sure how to work around these.
    try_add_compile_options(-Wno-error=class-memaccess)
    try_add_compile_options(-Wno-class-memaccess)

    # TODO: Should be addressed.
    try_add_compile_options(-Wno-error=missing-declarations)
    try_add_compile_options(-Wno-missing-declarations)
endif()
