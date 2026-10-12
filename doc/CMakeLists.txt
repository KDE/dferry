file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/doc)

# ### Not ideal: This will only run at CMake time, just running make / ninja etc will not trigger it.
#     Running CMake code at build time depending on file changes is ugly (invoke cmake -S on a separate
#     script file and don't forget to pass in all necessary state), using external commands without
#     relying on OS details is ugly, too.
if ("${PROJECT_SOURCE_DIR}/doc/inputPaths.dox" IS_NEWER_THAN "${PROJECT_BINARY_DIR}/doc/inputPaths.dox")
    file(READ "${PROJECT_SOURCE_DIR}/doc/inputPaths.dox" INPUT_PATHS_DOX)
    string(REPLACE " ../" " ${PROJECT_SOURCE_DIR}/" INPUT_PATHS_DOX "${INPUT_PATHS_DOX}")
    file(WRITE "${PROJECT_BINARY_DIR}/doc/inputPaths.dox" "${INPUT_PATHS_DOX}")
endif()

add_custom_target(doc
    # TODO pass VERSION=${VERSION} to doxygen?
    COMMAND "${CMAKE_COMMAND}" -E env GENERATE_HTML=YES doxygen "${PROJECT_SOURCE_DIR}/doc/Doxyfile"
    DEPENDS "${PROJECT_SOURCE_DIR}/doc/Doxyfile" "${PROJECT_BINARY_DIR}/doc/inputPaths.dox"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/doc/"
)
