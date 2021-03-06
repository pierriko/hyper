macro(HYPER_NODE node)

	project(HYPER_ABILITY_${node} CXX)
	enable_language(C)
	include(CheckIncludeFile)

	# use, i.e. don't skip the full RPATH for the build tree
	SET(CMAKE_SKIP_BUILD_RPATH  FALSE)

	# when building, don't use the install RPATH already
	# (but later on when installing)
	SET(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

	SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib/hyper")

	SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

	set(ARG_LIST ${ARGN})
	set(REQUIRED_LIBS "")
	list(LENGTH ARG_LIST LEN)
	if (LEN GREATER 1)
		list(REMOVE_AT ARG_LIST 0)
		foreach (lib ${ARG_LIST})
			find_library(hyper_${lib} libhyper_${lib}.so PATHS ${HYPER_LIB_DIR}/hyper/)
			if (NOT EXISTS ${hyper_${lib}})
				message(FATAL_ERROR "Can't find library ${hyper_${lib}}")
			endif()
			set(REQUIRED_LIBS "${REQUIRED_LIBS};${hyper_${lib}}")
		endforeach()
	endif()

	find_package(Boost 1.42 REQUIRED COMPONENTS system thread serialization filesystem date_time)
	set(BOOST_FOUND ${Boost_FOUND})
	include_directories(${Boost_INCLUDE_DIRS})
	message(STATUS "boost libraries " ${Boost_LIBRARIES})

	set(base_directory src/${node})

	if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${base_directory}/CMakeLists.txt)
		include(${CMAKE_CURRENT_SOURCE_DIR}/${base_directory}/CMakeLists.txt)
	endif()

	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
	include_directories(${HYPER_INCLUDE_DIRS})
	include_directories(${base_directory})

	set(USER_SRCS_FULL_PATH "")
	foreach(src ${USER_SRCS})
		set(USER_SRCS_FULL_PATH ${USER_SRCS_FULL_PATH} ${base_directory}/${src})
	endforeach(src)

	if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${base_directory}/import.cc)
		file(GLOB funcs ${base_directory}/funcs/*.cc)
		add_library(hyper_${node} SHARED ${funcs} ${base_directory}/import.cc ${USER_SRCS_FULL_PATH})
		target_link_libraries(hyper_${node} ${USER_LIBS})
		install(TARGETS hyper_${node} DESTINATION lib/hyper/)
		install(FILES ${base_directory}/funcs.hh ${base_directory}/import.hh
				DESTINATION include/hyper/${node})
	endif()

	set(SRC src/main.cc ${base_directory}/ability.cc)
	file(GLOB tasks ${base_directory}/tasks/*cc)
	set(SRC ${tasks} ${SRC})
	file(GLOB recipes ${base_directory}/recipes/*cc)
	set(SRC ${recipes} ${SRC})

	link_directories(${HYPER_LIB_DIR})
	add_executable(${node} ${SRC})
	set_property(TARGET ${node} PROPERTY OUTPUT_NAME hyper_${node})
	install(TARGETS ${node}
			 DESTINATION bin)
	install(FILES ${base_directory}/types.hh
			 DESTINATION include/hyper/${node})
	install(FILES ${node}.ability
			 DESTINATION share/hyper)
	if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${base_directory}/import.cc)
		target_link_libraries(${node} hyper_${node})
	endif()
	target_link_libraries(${node} ${Boost_LIBRARIES})
	target_link_libraries(${node} ${HYPER_LIBS})
	target_link_libraries(${node} ${REQUIRED_LIBS})

	add_executable(${node}_test src/test_main.cc)
	set_property(TARGET ${node}_test PROPERTY OUTPUT_NAME hyper_${node}_test)
	target_link_libraries(${node}_test ${Boost_LIBRARIES} ${HYPER_LIBS})
	install(TARGETS ${node}_test DESTINATION bin)
endmacro(HYPER_NODE)
