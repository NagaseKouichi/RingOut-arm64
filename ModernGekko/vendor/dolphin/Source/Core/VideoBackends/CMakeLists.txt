add_subdirectory(OGL)
add_subdirectory(Null)
add_subdirectory(Software)

# MSVC, not WIN32. The D3D backends need C++/WinRT (Common/HRWrap.h includes
# <winrt/base.h>) and the DirectX SDK pieces that come with Visual Studio;
# MinGW has neither. HAS_D3D follows the HAS_VULKAN/HAS_OPENGL pattern so
# VideoBackendBase can register them only when they exist.
if(MSVC)
  add_subdirectory(D3DCommon)
  add_subdirectory(D3D)
  add_subdirectory(D3D12)
  add_definitions(-DHAS_D3D)
endif()

if(APPLE)
  add_subdirectory(Metal)
endif()

if(ENABLE_VULKAN)
  add_subdirectory(Vulkan)
endif()
