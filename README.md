# Zephyr Project Build Guide (RP2350 + W6300)

이 문서는 Zephyr 워크스페이스 외부에 위치한 프로젝트(`ethw6300`)를 빌드하기 위한 환경 설정 및 명령어 가이드입니다.

## 1. 전제 조건 (Prerequisites)

이 프로젝트를 빌드하기 위해서는 다음 환경이 구성되어 있어야 합니다.

*   **Zephyr SDK**: 툴체인 및 호스트 툴 설치 완료
*   **West**: Zephyr 메타 툴 설치 완료
*   **Python venv**: 프로젝트용 가상 환경 구성 완료

## 2. 디렉토리 구조 (Directory Structure)

Zephyr 표준 워크스페이스(Topology 2) 구조를 따르지 않고, 독립된 폴더에서 개발하는 경우입니다.

```text
~/workspace/rpi-zephyr/
├── zephyrproject/           # Zephyr 공식 워크스페이스 (커널, 모듈 등)
│   ├── zephyr/
│   ├── modules/
│   └── .west/
└── ethw6300/                # 나의 애플리케이션 프로젝트 (현재 위치)
    ├── CMakeLists.txt       # 빌드 설정 (Pico SDK 경로 패치 포함)
    ├── prj.conf             # 커널/드라이버 설정 (Kconfig)
    ├── boards/              # 커스텀 보드 정의
    ├── drivers/             # 커스텀 드라이버 소스
    └── src/                 # 메인 소스
```

## 3. 빌드 방법 (How to Build)

외부 프로젝트를 빌드할 때는 **Zephyr 워크스페이스 루트**에서 명령을 실행해야 합니다.

### 3.1. 환경 설정 (매번 실행)
먼저 Python 가상 환경을 활성화합니다.
```bash
source ~/workspace/rpi-zephyr/ethw6300/.venv/bin/activate
```

### 3.2. 빌드 명령어
다음 명령어를 한 줄로 실행합니다.

```bash
# Zephyr 워크스페이스로 이동 후 빌드 실행
cd ~/workspace/rpi-zephyr/zephyrproject

west build -p always \
  -s ~/workspace/rpi-zephyr/ethw6300 \
  -b rp2350_w6300_evb/rp2350a/hazard3 \
  -- -DBOARD_ROOT=~/workspace/rpi-zephyr/ethw6300
```

*   `-p always`: 항상 깨끗한 상태에서 다시 빌드 (Pristine build)
*   `-s <path>`: 소스(애플리케이션) 디렉토리 지정
*   `-b <board>`: 타겟 보드 이름
*   `-DBOARD_ROOT=<path>`: 커스텀 보드 정의가 있는 디렉토리 지정 (필수)

west build -p always -s ~/workspace/rpi-zephyr/ethw6300 -b rp2350_w6300_evb/rp2350a/hazard3 -- -DBOARD_ROOT=~/workspace/rpi-zephyr/ethw6300

/home/suzinee_u/workspace/rpi-zephyr/ethw6300/.venv/bin/west build -p always -s /home/suzinee_u/workspace/rpi-zephyr/ethw6300 -b rp2350_w6300_evb/rp2350a/hazard3 -- -DBOARD_ROOT=/home/suzinee_u/workspace/rpi-zephyr/ethw6300 

west build -p always -b w6300_evb_pico2/rp2350a/m33 ../ethw6300 -- -DBOARD_ROOT=/home/suzinee_u/workspace/rpi-zephyr/workspace

### 3.3. 빌드 결과물
빌드가 성공하면 다음 경로에 펌웨어 파일이 생성됩니다.
*   `~/workspace/rpi-zephyr/zephyrproject/build/zephyr/zephyr.uf2`

이 파일을 RP2350 보드(부트로더 모드)에 복사하여 실행할 수 있습니다.

## 4. 핵심 설정 파일 설명

### `CMakeLists.txt`
RP2350 환경에서 `hal_rpi_pico` 모듈의 헤더 경로가 자동으로 잡히지 않는 문제를 해결하기 위해 다음 코드가 추가되었습니다.

```cmake
# Pico SDK의 하드웨어 제어 헤더 경로 강제 추가
zephyr_include_directories(
    ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/rp2_common/hardware_gpio/include
    ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/rp2_common/hardware_base/include
    ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/common/pico_base_headers/include
)
```

### `prj.conf`
W6300 제어를 위한 필수 설정입니다.

```properties
# Networking & Ethernet
CONFIG_NETWORKING=y
CONFIG_ETH_W6300=y

# SPI Interface (Software Bitbang 사용 시)
CONFIG_SPI=y
CONFIG_SPI_BITBANG=y
CONFIG_GPIO=y
```

```