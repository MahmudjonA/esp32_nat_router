FILE(GLOB_RECURSE app_sources
    ${CMAKE_SOURCE_DIR}/src/*.c
)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    REQUIRES tg_control
)



target_add_binary_data(${COMPONENT_TARGET} "pages/favicon.ico" BINARY)
target_add_binary_data(${COMPONENT_TARGET} "pages/styles-67aa3b0203355627b525be2ea57be7bf.css" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "pages/config.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "pages/result.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "pages/apply.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "pages/reset.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "pages/unlock.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "pages/advanced.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "pages/lock.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "pages/clients.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "pages/jquery-8a1045d9cbf50b52a0805c111ba08e94.js" TEXT)