enable_testing()
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME} --test)

add_custom_target(generate_mocks
        DEPENDS ${MOCK_OUTPUTS}
)
add_dependencies(${PROJECT_NAME} generate_mocks)

# Add sanitizers: Please comment out when debugging
target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address -fsanitize=undefined)
target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=address -fsanitize=undefined)
