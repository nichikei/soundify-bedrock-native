# Use as CMAKE_PROJECT_Latite_INCLUDE when configuring the pinned Latite checkout.
# All generated modifications are in the build directory; upstream stays intact.
set(SOUNDIFY_INTEGRATION_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "Soundify integration")
function(soundify_integrate)
    execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE upstream_commit OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(NOT upstream_commit STREQUAL "9f7463515dd298a496da918285936d78c7416aad")
        message(FATAL_ERROR "Unreviewed Latite commit: ${upstream_commit}")
    endif()
    if(NOT TARGET Latite)
        message(FATAL_ERROR "Latite target missing")
    endif()
    get_filename_component(soundify_root "${SOUNDIFY_INTEGRATION_DIR}/../.." ABSOLUTE)
    set(manager "${CMAKE_SOURCE_DIR}/src/client/feature/module/ModuleManager.cpp")
    file(READ "${manager}" contents)
    string(FIND "${contents}" "ModuleManager::ModuleManager() {" marker)
    if(marker EQUAL -1)
        message(FATAL_ERROR "Latite ModuleManager changed; review integration before building")
    endif()
    string(REPLACE "ModuleManager::ModuleManager() {"
        "ModuleManager::ModuleManager() {\n    this->items.push_back(std::make_shared<SoundifyModule>());"
        contents "${contents}")
    file(WRITE "${CMAKE_BINARY_DIR}/SoundifyModuleManager.cpp"
        "#include \"pch.h\"\n#include \"SoundifyModule.h\"\n${contents}")
    set_source_files_properties("${manager}" PROPERTIES HEADER_FILE_ONLY TRUE)
    # MSVC 19.43 needs an explicit parameter list before a lambda trailing return.
    set(keystrokes "${CMAKE_SOURCE_DIR}/src/client/feature/module/modules/hud/Keystrokes.cpp")
    file(READ "${keystrokes}" keystrokes_contents)
    string(REPLACE "[&] -> bool" "[&]() -> bool" keystrokes_contents "${keystrokes_contents}")
    file(WRITE "${CMAKE_BINARY_DIR}/SoundifyKeystrokes.cpp" "${keystrokes_contents}")
    set_source_files_properties("${keystrokes}" PROPERTIES HEADER_FILE_ONLY TRUE)
    # Receive actual Windows text input without changing the pinned checkout.
    set(general_hooks "${CMAKE_SOURCE_DIR}/src/client/memory/hook/hooks/GeneralHooks.cpp")
    file(READ "${general_hooks}" general_contents)
    set(key_marker "    if (msg == WM_KEYDOWN || msg == WM_KEYUP) {")
    string(FIND "${general_contents}" "${key_marker}" key_position)
    if(key_position EQUAL -1)
        message(FATAL_ERROR "Latite window input hook changed; review native text integration")
    endif()
    string(REPLACE "${key_marker}"
        "    if (msg == WM_CHAR) {\n        NativeTextInputEvent textEvent{static_cast<std::uint32_t>(wParam)};\n        Eventing::get().dispatch(textEvent);\n    }\n\n${key_marker}"
        general_contents "${general_contents}")
    file(WRITE "${CMAKE_BINARY_DIR}/SoundifyGeneralHooks.cpp"
        "#include \"pch.h\"\n#include \"NativeTextInputEvent.h\"\n${general_contents}")
    set_source_files_properties("${general_hooks}" PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(Latite PRIVATE
        "${CMAKE_BINARY_DIR}/SoundifyGeneralHooks.cpp"
        "${CMAKE_BINARY_DIR}/SoundifyKeystrokes.cpp"
        "${CMAKE_BINARY_DIR}/SoundifyModuleManager.cpp"
        "${SOUNDIFY_INTEGRATION_DIR}/SoundifyModule.cpp"
        "${soundify_root}/src/core/ExplosionRouter.cpp"
        "${soundify_root}/src/core/SoundEngine.cpp"
        "${soundify_root}/src/client/SoundifyClient.cpp"
        "${soundify_root}/src/security/InstallationIdentity.cpp"
        "${soundify_root}/src/security/MachineId.cpp"
        "${soundify_root}/src/security/ProtectedTokenStore.cpp"
        "${soundify_root}/src/security/AuthClient.cpp")
    target_include_directories(Latite PRIVATE "${SOUNDIFY_INTEGRATION_DIR}"
        "${soundify_root}/include" "${CMAKE_SOURCE_DIR}/src/client/feature/module"
        "${CMAKE_SOURCE_DIR}/src/client/feature/module/modules/hud"
        "${CMAKE_SOURCE_DIR}/src/client/memory/hook/hooks")
    target_compile_options(Latite PRIVATE /utf-8)
    target_link_libraries(Latite PRIVATE bcrypt ncrypt crypt32 winhttp wbemuuid ole32 oleaut32)
endfunction()
cmake_language(DEFER CALL soundify_integrate)
