# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_executable(test_account_indices)
init_target(test_account_indices "(tests)")

target_include_directories(test_account_indices PRIVATE ${src_loc})
nice_target_sources(test_account_indices ${src_loc}
PRIVATE
    storage/storage_account_indices.h
    tests/test_account_indices.cpp
)
target_link_libraries(test_account_indices PRIVATE desktop-app::external_qt)
if (WIN32 AND DESKTOP_APP_WIN7_BASELINE)
    target_link_libraries(test_account_indices PRIVATE desktop-app::lib_base)
endif()

set_target_properties(test_account_indices PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    EXCLUDE_FROM_DEFAULT_BUILD TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
