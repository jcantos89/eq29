# The pinned DPF-Widgets source uses two GNU omitted-middle ternaries (x ?: y),
# unsupported by MSVC. Generate a standards-compliant compilation copy instead
# of editing or forking the pinned dependency. Include paths remain unchanged.
set(imgui_upstream "${CMAKE_CURRENT_SOURCE_DIR}/widgets/opengl/DearImGui.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${imgui_upstream}")
file(READ "${imgui_upstream}" imgui_source)
string(REPLACE "self->getWidth() ?: 640 * scaleFactor"
  "self->getWidth() ? self->getWidth() : 640 * scaleFactor" imgui_source "${imgui_source}")
string(REPLACE "self->getHeight() ?: 480 * scaleFactor"
  "self->getHeight() ? self->getHeight() : 480 * scaleFactor" imgui_source "${imgui_source}")
set(EQ29_IMGUI_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/DearImGui.cpp")
set(existing_imgui "")
if(EXISTS "${EQ29_IMGUI_SOURCE}")
  file(READ "${EQ29_IMGUI_SOURCE}" existing_imgui)
endif()
if(NOT existing_imgui STREQUAL imgui_source)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
  file(WRITE "${EQ29_IMGUI_SOURCE}" "${imgui_source}")
endif()
