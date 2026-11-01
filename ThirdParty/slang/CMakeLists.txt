set(ROXELL_SLANG_ROOT "" CACHE PATH "Optional extracted Slang SDK for offline builds")

if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND WIN32)
    set(slang_archive "slang-2026.18-windows-x86_64.zip")
    set(slang_sha256 "6ffa4827b519fd0a85b38407049d87ab0c1f045fe2289cb1e6831f965169f8a1")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(slang_archive "slang-2026.18-linux-x86_64-glibc-2.27.tar.gz")
    set(slang_sha256 "e45ea4f117d51b8c1e84fa49f562081e73a9f29d02bd4f7fad20678603282829")
else()
    message(FATAL_ERROR "Slang requires a Windows x64 or Linux x64 package")
endif()

if(NOT ROXELL_SLANG_ROOT)
    include(FetchContent)
    FetchContent_Declare(roxell_slang
        URL "https://github.com/shader-slang/slang/releases/download/v2026.18/${slang_archive}"
        URL_HASH "SHA256=${slang_sha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(roxell_slang)
    set(ROXELL_SLANG_ROOT "${roxell_slang_SOURCE_DIR}")
endif()

if(WIN32)
    set(slang_library "${ROXELL_SLANG_ROOT}/lib/slang.lib")
    set(slang_runtime_files
        "${ROXELL_SLANG_ROOT}/bin/slang.dll"
        "${ROXELL_SLANG_ROOT}/bin/slang-compiler.dll"
        "${ROXELL_SLANG_ROOT}/bin/slang-glsl-module.dll"
        "${ROXELL_SLANG_ROOT}/bin/slang-glslang.dll"
    )
else()
    set(slang_library "${ROXELL_SLANG_ROOT}/lib/libslang-compiler.so.0.2026.18")
    set(slang_runtime_files
        "${slang_library}"
        "${ROXELL_SLANG_ROOT}/lib/libslang-glsl-module-2026.18.so"
        "${ROXELL_SLANG_ROOT}/lib/libslang-glslang-2026.18.so"
    )
endif()

foreach(slang_file IN ITEMS "${ROXELL_SLANG_ROOT}/include/slang.h" "${slang_library}" ${slang_runtime_files})
    if(NOT EXISTS "${slang_file}")
        message(FATAL_ERROR "The Slang package is incomplete: ${slang_file}")
    endif()
endforeach()

add_library(ThirdParty-Slang UNKNOWN IMPORTED GLOBAL)
set_target_properties(ThirdParty-Slang PROPERTIES
    IMPORTED_LOCATION "${slang_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${ROXELL_SLANG_ROOT}/include"
    ROXELL_RUNTIME_FILES "${slang_runtime_files}"
    FOLDER "External"
)
function(roxell_copy_slang_runtime target)
	get_target_property(runtime_files ThirdParty-Slang ROXELL_RUNTIME_FILES)
	get_property(multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
	if(multi)
		set(destination "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
	else()
		set(destination "${CMAKE_CURRENT_BINARY_DIR}")
	endif()
	string(SHA256 destination_hash "${CMAKE_CURRENT_BINARY_DIR}")
	string(SUBSTRING "${destination_hash}" 0 12 destination_hash)
	set(stage_target "Build-SlangRuntime-${destination_hash}")
	if(NOT TARGET ${stage_target})
		set(staged_files)
		foreach(runtime_file IN LISTS runtime_files)
			get_filename_component(runtime_name "${runtime_file}" NAME)
			set(staged "${destination}/${runtime_name}")
			add_custom_command(OUTPUT "${staged}" COMMAND ${CMAKE_COMMAND} -E copy_if_different "${runtime_file}" "${staged}" DEPENDS "${runtime_file}" VERBATIM)
			list(APPEND staged_files "${staged}")
		endforeach()
		add_custom_target(${stage_target} DEPENDS ${staged_files})
		set_target_properties(${stage_target} PROPERTIES FOLDER "External" ROXELL_STAGED_FILES "${staged_files}")
	endif()
	add_dependencies(${target} ${stage_target})
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
	endif()
	get_target_property(ROXELL_SLANG_STAGED_RUNTIME_FILES ${stage_target} ROXELL_STAGED_FILES)
	set(ROXELL_SLANG_STAGED_RUNTIME_FILES "${ROXELL_SLANG_STAGED_RUNTIME_FILES}" PARENT_SCOPE)
endfunction()
