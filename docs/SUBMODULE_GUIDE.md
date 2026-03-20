# SUBMODULE_GUIDE — sv_assignment_core_module

## 1. 개요

`sv_assignment_core_module` 은 controller 와 agent 두 Docker 이미지가 공유하는 라이브러리 저장소.
`sv_assignment/src/libs/` 에 git 서브모듈로 마운트된다.

```
sv_assignment_core_module/
├── cmake/setup.cmake    ← _add_package_sv_libs() 매크로
├── core/                ← 공용 인터페이스 + 구현
└── logger/              ← JSON Lines 로거
```

## 2. 서브모듈 초기화

```bash
# 클론 시 함께
git clone --recurse-submodules https://github.com/jinwoo-Sync/sv_assignment_core_module.git

# 이미 클론된 경우
git submodule update --init --recursive
```

## 3. .gitmodules

```ini
[submodule "src/libs"]
    path   = src/libs
    url    = https://github.com/jinwoo-Sync/sv_assignment_core_module.git
    branch = main
```

## 4. setup.cmake 패턴 (replicapackage 차용)

```cmake
# src/libs/cmake/setup.cmake
include_guard(GLOBAL)

if(NOT DEFINED BASE_DIRECTORY)
  get_filename_component(BASE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

macro(_add_package_sv_libs)
  include_directories(${BASE_DIRECTORY}/libs/logger/include)
  if(NOT TARGET sv_logger)
    add_subdirectory(${BASE_DIRECTORY}/libs/logger ${CMAKE_BINARY_DIR}/libs/logger)
  endif()

  include_directories(${BASE_DIRECTORY}/libs/core/include)
  if(NOT TARGET sv_core)
    add_subdirectory(${BASE_DIRECTORY}/libs/core ${CMAKE_BINARY_DIR}/libs/core)
  endif()
endmacro()
```

## 5. 루트 CMakeLists.txt 사용 예

```cmake
# src/CMakeLists.txt
if(NOT DEFINED BASE_DIRECTORY)
  get_filename_component(BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
endif()

include(${BASE_DIRECTORY}/libs/cmake/setup.cmake)
_add_package_sv_libs()

add_subdirectory(controller)
add_subdirectory(agent)
```

## 6. Docker 빌드 시 서브모듈 포함

```yaml
# docker-compose.yml
services:
  controller:
    build:
      context: ./src          # libs/ 포함
      dockerfile: controller/Dockerfile
  agent:
    build:
      context: ./src          # libs/ 포함
      dockerfile: agent/Dockerfile
```

