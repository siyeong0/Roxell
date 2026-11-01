set(ROXELL_DXC_ROOT "" CACHE PATH "Optional extracted DXC package for offline builds")

if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND WIN32)
    set(dxc_archive "dxc_2026_05_27.zip")
    set(dxc_sha256 "cf658aacf070d3045e31b8f1f8a696c2945f37c1095019481ef7c513368db3b4")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(dxc_archive "linux_dxc_2026_05_26.x86_64.tar.gz")
    set(dxc_sha256 "928b3e9986d11dc4279050e02340950c29bcbd1e5efb9d3ded9669dade37639d")
else()
    message(FATAL_ERROR "DXC requires a Windows x64 or Linux x64 package")
endif()

if(NOT ROXELL_DXC_ROOT)
    include(FetchContent)
    FetchContent_Declare(roxell_dxc
        URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602.24/${dxc_archive}"
        URL_HASH "SHA256=${dxc_sha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(roxell_dxc)
    set(ROXELL_DXC_ROOT "${roxell_dxc_SOURCE_DIR}")
endif()

if(WIN32)
    set(ROXELL_DXC_INCLUDE_DIRECTORY "${ROXELL_DXC_ROOT}/inc")
    set(ROXELL_DXC_LIBRARY "${ROXELL_DXC_ROOT}/lib/x64/dxcompiler.lib")
    set(ROXELL_DXC_RUNTIME_FILES
        "${ROXELL_DXC_ROOT}/bin/x64/dxcompiler.dll"
        "${ROXELL_DXC_ROOT}/bin/x64/dxil.dll"
    )
    set(dxc_api_header "${ROXELL_DXC_INCLUDE_DIRECTORY}/dxcapi.h")
else()
    set(ROXELL_DXC_INCLUDE_DIRECTORY "${ROXELL_DXC_ROOT}/include")
    set(ROXELL_DXC_LIBRARY "${ROXELL_DXC_ROOT}/lib/libdxcompiler.so")
    set(ROXELL_DXC_RUNTIME_FILES
        "${ROXELL_DXC_ROOT}/lib/libdxcompiler.so"
        "${ROXELL_DXC_ROOT}/lib/libdxil.so"
    )
    set(dxc_api_header "${ROXELL_DXC_INCLUDE_DIRECTORY}/dxc/dxcapi.h")
endif()

foreach(dxc_file IN ITEMS "${dxc_api_header}" "${ROXELL_DXC_LIBRARY}" ${ROXELL_DXC_RUNTIME_FILES})
    if(NOT EXISTS "${dxc_file}")
        message(FATAL_ERROR "The DXC package is incomplete: ${dxc_file}")
    endif()
endforeach()

add_library(ThirdParty-Dxc UNKNOWN IMPORTED GLOBAL)
set_target_properties(ThirdParty-Dxc PROPERTIES
	IMPORTED_LOCATION "${ROXELL_DXC_LIBRARY}"
	INTERFACE_INCLUDE_DIRECTORIES "${ROXELL_DXC_INCLUDE_DIRECTORY}"
	ROXELL_RUNTIME_FILES "${ROXELL_DXC_RUNTIME_FILES}"
)
set_property(TARGET ThirdParty-Dxc PROPERTY FOLDER "External")

set_property(GLOBAL PROPERTY ROXELL_DXC_LIBRARY_PATH "${ROXELL_DXC_LIBRARY}")

function(roxell_copy_dxc_runtime target)
	get_target_property(runtime_files ThirdParty-Dxc ROXELL_RUNTIME_FILES)
	if(NOT runtime_files)
		message(FATAL_ERROR "ThirdParty-Dxc does not declare its required runtime files.")
	endif()

	get_property(roxell_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
	if(roxell_multi_config)
		set(destination "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
		set(claim "${CMAKE_CURRENT_BINARY_DIR}/multi")
	else()
		set(destination "${CMAKE_CURRENT_BINARY_DIR}")
		set(claim "${CMAKE_CURRENT_BINARY_DIR}/single")
	endif()

	string(SHA256 claim_hash "${claim}")
	string(SUBSTRING "${claim_hash}" 0 12 short_hash)
	set(stage_target "Build-DxcRuntime-${short_hash}")
	if(NOT TARGET ${stage_target})
		set(staged_files)
		foreach(runtime_file IN LISTS runtime_files)
			get_filename_component(runtime_name "${runtime_file}" NAME)
			set(staged "${destination}/${runtime_name}")
			add_custom_command(
				OUTPUT "${staged}"
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "${runtime_file}" "${staged}"
				DEPENDS "${runtime_file}"
				COMMENT "Staging ${runtime_name} into ${destination}"
				VERBATIM
			)
			list(APPEND staged_files "${staged}")
		endforeach()
		add_custom_target(${stage_target} DEPENDS ${staged_files})
		set_property(TARGET ${stage_target} PROPERTY FOLDER "External")
		set_property(TARGET ${stage_target} PROPERTY
			ROXELL_DXC_STAGED_FILES "${staged_files}")
	endif()

	add_dependencies(${target} ${stage_target})
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
	endif()

	get_property(roxell_staged TARGET ${stage_target} PROPERTY ROXELL_DXC_STAGED_FILES)
	set(ROXELL_DXC_STAGED_RUNTIME_FILES "${roxell_staged}" PARENT_SCOPE)
endfunction()
