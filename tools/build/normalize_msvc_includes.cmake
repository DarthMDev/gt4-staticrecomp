# Ninja matches MSVC include metadata byte-for-byte. Decode the compiler's
# UTF-8 output, then emit an ASCII prefix independent of language packs.
# Invocation: cmake -DINCLUDE_PREFIX=... -P this_file -- compiler arguments.
if(NOT DEFINED INCLUDE_PREFIX OR INCLUDE_PREFIX STREQUAL "")
    message(FATAL_ERROR "The MSVC /showIncludes prefix was not detected")
endif()

set(compiler_arguments)
set(after_separator FALSE)
math(EXPR last_argument "${CMAKE_ARGC} - 1")
foreach(argument_index RANGE 0 ${last_argument})
    set(argument "${CMAKE_ARGV${argument_index}}")
    if(after_separator)
        # A trailing backslash in /Fd's directory would escape the CMake
        # list separator and merge the next flag into the PDB path.
        if(argument MATCHES "^/Fd")
            string(REPLACE "\\" "/" argument "${argument}")
        endif()
        # CMake lists cannot preserve these arguments through COMMAND expansion.
        # Reject them explicitly rather than change the compiler invocation.
        if(argument MATCHES ";" OR argument MATCHES "\\\\$")
            message(FATAL_ERROR "Unsupported semicolon or trailing backslash in compiler argument: ${argument}")
        endif()
        list(APPEND compiler_arguments "${argument}")
    elseif(argument STREQUAL "--")
        set(after_separator TRUE)
    endif()
endforeach()
if(NOT compiler_arguments)
    message(FATAL_ERROR "No compiler command supplied to the MSVC launcher")
endif()

execute_process(COMMAND ${compiler_arguments}
    RESULT_VARIABLE compiler_result
    OUTPUT_VARIABLE compiler_output
    ERROR_VARIABLE compiler_error
    ENCODING UTF-8)
function(normalize_include_metadata output_name input)
    # Prefix a newline so the first line and later lines use the same rule.
    # An example of the prefix inside an ordinary diagnostic is left intact.
    string(REPLACE "\n${INCLUDE_PREFIX}" "\nNote: including file: "
        normalized "\n${input}")
    string(SUBSTRING "${normalized}" 1 -1 normalized)
    set(${output_name} "${normalized}" PARENT_SCOPE)
endfunction()
normalize_include_metadata(compiler_output "${compiler_output}")
normalize_include_metadata(compiler_error "${compiler_error}")
if(NOT compiler_output STREQUAL "")
    message("${compiler_output}")
endif()
if(NOT compiler_error STREQUAL "")
    message("${compiler_error}")
endif()
if(NOT compiler_result EQUAL 0)
    message(FATAL_ERROR "MSVC compiler failed: ${compiler_result}")
endif()
# A successful compiler with an unrecognized metadata prefix must not leave
# Ninja believing that the object has no header dependencies.
set(unrecognized_include "(^|\n)[^:\n]+:[^:\n]+:[ \t]+([A-Za-z]:[/\\]|[/\\][/\\])")
string(REPLACE "\nNote: including file: " "\n" remaining_output
    "\n${compiler_output}\n${compiler_error}")
if(remaining_output MATCHES "${unrecognized_include}")
    message(FATAL_ERROR "Unrecognized MSVC include metadata; check compiler output encoding and detected prefix")
endif()
