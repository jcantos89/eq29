# DPF's MSVC backend needs extension types absent from the Windows OpenGL 1.1
# SDK. Populate its documented ignored header cache before DPF's legacy,
# unchecked khronos.org downloads can create empty headers. Pin both upstream
# revision and checksum, retain TLS verification, and fail during configuration
# if a header cannot be verified.
if(NOT DEFINED EQ29_OPENGL_HEADER_ROOT)
  set(EQ29_OPENGL_HEADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/dpf/khronos")
endif()
function(eq29_verified_gl_header relative url digest)
  set(destination "${EQ29_OPENGL_HEADER_ROOT}/${relative}")
  if(EXISTS "${destination}")
    file(SHA256 "${destination}" existing_hash)
    if(existing_hash STREQUAL digest)
      return()
    endif()
  endif()
  get_filename_component(parent "${destination}" DIRECTORY)
  file(MAKE_DIRECTORY "${parent}")
  file(DOWNLOAD "${url}" "${destination}.download"
    EXPECTED_HASH "SHA256=${digest}" TLS_VERIFY ON STATUS status)
  list(GET status 0 result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot download verified OpenGL header ${relative}: ${status}")
  endif()
  file(RENAME "${destination}.download" "${destination}")
endfunction()
eq29_verified_gl_header(GL/glext.h
  "https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/6af574a14089ccfee87efe230ebcdd8742859813/api/GL/glext.h"
  "d7b6f15b1bb209e08bd6ada36983f8bb40429e66c24083aa2474d55205d97047")
eq29_verified_gl_header(KHR/khrplatform.h
  "https://raw.githubusercontent.com/KhronosGroup/EGL-Registry/8e56e86e65687697ef54114cdb1afabdfac756bf/api/KHR/khrplatform.h"
  "7b1e01aaa7ad8f6fc34b5c7bdf79ebf5189bb09e2c4d2e79fc5d350623d11e83")
