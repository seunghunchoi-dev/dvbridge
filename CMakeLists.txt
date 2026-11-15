# SPDX-License-Identifier: MIT
# Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
# DVBridge — DMR 마스터 접속 + USRP 브릿지 (독자 작성, DroidStar 코드 미사용)
cmake_minimum_required(VERSION 3.16)
project(DVBridge VERSION 0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- core/dmr : 순수 C++ (Qt 불필요) ----
add_library(dmr_core STATIC
    core/dmr/fec.cpp core/dmr/fec.h
    core/dmr/burst.cpp core/dmr/burst.h
    core/dmr/voice.cpp core/dmr/voice.h
    core/dmr/talker_alias.cpp core/dmr/talker_alias.h
)
target_include_directories(dmr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)

# ---- 시험 ----
add_executable(dmr_core_test tests/dmr_core_test.cpp)
target_link_libraries(dmr_core_test PRIVATE dmr_core)

# ---- core/net : Qt Network ----
find_package(Qt6 REQUIRED COMPONENTS Core Network)
qt_standard_project_setup()
add_library(net_core STATIC core/net/homebrew_client.cpp core/net/homebrew_client.h)
target_link_libraries(net_core PUBLIC dmr_core Qt::Core Qt::Network)
set_target_properties(net_core PROPERTIES AUTOMOC ON)

add_executable(homebrew_test tests/homebrew_test.cpp)
target_link_libraries(homebrew_test PRIVATE net_core)

# ---- 보코더·USRP·세션 ----
add_library(bridge_core STATIC
    core/vocoder/vocoder_host.cpp core/vocoder/vocoder_host.h
    core/audio/usrp_port.cpp core/audio/usrp_port.h
    core/session/session.cpp core/session/session.h
    core/session/call_log.cpp core/session/call_log.h
    core/session/id_db.cpp core/session/id_db.h
    core/net/id_db_updater.cpp core/net/id_db_updater.h
    core/web/dashboard_server.cpp core/web/dashboard_server.h core/web/dashboard_page.cpp core/web/settings_page.cpp
    core/web/sys_monitor.cpp core/web/sys_monitor.h
)
target_include_directories(bridge_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core ${CMAKE_CURRENT_SOURCE_DIR}/plugin_api)
target_link_libraries(bridge_core PUBLIC net_core Qt::Core Qt::Network psapi)
set_target_properties(bridge_core PROPERTIES AUTOMOC ON)

add_executable(session_test tests/session_test.cpp)
target_link_libraries(session_test PRIVATE bridge_core)

# ---- 간이 실행기 (화면 버전 전까지) ----
add_executable(dvbridge_cli app/cli_main.cpp)
target_link_libraries(dvbridge_cli PRIVATE app_core)

# ---- Windows 화면 (D단계) ----
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_library(app_core STATIC app/app_settings.cpp app/app_settings.h app/net_check.cpp app/net_check.h app/notice_service.cpp app/notice_service.h)
target_include_directories(app_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_link_libraries(app_core PUBLIC bridge_core crypt32)

# 아이콘·버전 정보 리소스 (아이콘은 app/res/make_icon.py로 직접 그린 것)
enable_language(RC)
set(ICON_PATH ${CMAKE_CURRENT_SOURCE_DIR}/app/res/dvbridge.ico)
configure_file(app/res/dvbridge.rc.in ${CMAKE_CURRENT_BINARY_DIR}/dvbridge.rc @ONLY)
add_executable(DVBridge WIN32
    app/gui_main.cpp
    app/main_window.cpp app/main_window.h
    ${CMAKE_CURRENT_BINARY_DIR}/dvbridge.rc
)
target_link_libraries(DVBridge PRIVATE app_core Qt::Widgets Qt::Network)
# 트레이·창 아이콘 (Qt 리소스)
qt_add_resources(DVBridge "icons" PREFIX "/" BASE app/res FILES app/res/dvbridge.png)
set_target_properties(DVBridge PROPERTIES AUTOMOC ON)
target_compile_definitions(DVBridge PRIVATE DVBRIDGE_VERSION="${PROJECT_VERSION}")

add_executable(settings_test tests/settings_test.cpp)
target_link_libraries(settings_test PRIVATE app_core)

add_executable(notice_test tests/notice_test.cpp)
target_link_libraries(notice_test PRIVATE app_core)

add_executable(dashboard_test tests/dashboard_test.cpp)
target_link_libraries(dashboard_test PRIVATE bridge_core)

add_executable(id_db_test tests/id_db_test.cpp)
target_link_libraries(id_db_test PRIVATE bridge_core)

add_executable(multiuser_test tests/multiuser_test.cpp)
target_link_libraries(multiuser_test PRIVATE bridge_core)

# ---- 음성 팩 도우미 (자연스러운 목소리, 별도 배포: 모델·ONNX Runtime은 음성 팩에만) ----
add_executable(dvbridge_tts tools/voicepack/dvbridge_tts.cpp)
target_include_directories(dvbridge_tts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/include)
target_compile_definitions(dvbridge_tts PRIVATE _USE_MATH_DEFINES UNICODE _UNICODE)
target_link_options(dvbridge_tts PRIVATE -municode -static -static-libgcc -static-libstdc++)
target_link_libraries(dvbridge_tts PRIVATE normaliz)

# ---- 자체 AMBE+2 소프트웨어 보코더 (플러그인 DLL, 본체와 별도 배포) ----
# 공개 저장소에는 보코더 소스가 없음 → 폴더가 있을 때만 빌드
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/plugins/vocoder_ambe2/vocoder_ambe2.cpp)
add_library(ambe2_codec STATIC
    plugins/vocoder_ambe2/ambe2_decoder.cpp plugins/vocoder_ambe2/ambe2_decoder.h
    plugins/vocoder_ambe2/ambe2_encoder.cpp plugins/vocoder_ambe2/ambe2_encoder.h
    plugins/vocoder_ambe2/ambe2_tables.cpp plugins/vocoder_ambe2/ambe2_tables.h
)
target_include_directories(ambe2_codec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugins/vocoder_ambe2)
set_target_properties(ambe2_codec PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC OFF)
target_compile_options(ambe2_codec PRIVATE -O2)
add_library(vocoder_ambe2 SHARED plugins/vocoder_ambe2/vocoder_ambe2.cpp)
target_include_directories(vocoder_ambe2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plugin_api)
target_link_libraries(vocoder_ambe2 PRIVATE ambe2_codec)
set_target_properties(vocoder_ambe2 PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden AUTOMOC OFF)
if(MINGW)
    target_link_options(vocoder_ambe2 PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
add_executable(ambe2_test tests/ambe2_test.cpp)
target_include_directories(ambe2_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plugin_api)
target_link_libraries(ambe2_test PRIVATE ambe2_codec)
set_target_properties(ambe2_test PROPERTIES AUTOMOC OFF)
endif()
