# 시행착오 기록 (Trials & Errors)

이 문서는 Zephyr 기반의 외부 프로젝트(`ethw6300`)를 빌드하면서 겪은 문제들과 해결 과정을 기록합니다.

## 1. West 실행 위치 문제
### 증상
애플리케이션 디렉토리에서 `west build` 실행 시 에러 발생.
```bash
west: unknown command "build"; do you need to run this inside a workspace?
```
### 원인
`west` 툴은 Zephyr 워크스페이스(`.west` 폴더가 있는 곳) 내부에서 실행되어야 함. 현재 프로젝트는 워크스페이스 외부에 위치함.
### 해결 시도
*   환경 변수 `ZEPHYR_BASE` 설정 후 `cmake`와 `ninja` 직접 호출 (Freestanding 방식) 시도. -> 헤더 경로 문제로 이어짐.

## 2. 네트워킹 스택 의존성 누락
### 증상
링킹 단계에서 `z_impl_sys_rand_get` 정의되지 않음 에러.
### 원인
네트워킹 기능을 켰으나(`CONFIG_NETWORKING=y`), 난수 생성기(Entropy/Random Generator) 드라이버가 활성화되지 않음.
### 해결
`prj.conf`에 다음 설정 추가:
```properties
CONFIG_ENTROPY_GENERATOR=y
CONFIG_TEST_RANDOM_GENERATOR=y
```

## 3. Pico SDK 헤더 파일 경로 문제 (가장 큰 난관)
### 증상
컴파일 중 `fatal error: hardware/gpio.h: No such file or directory` 발생.
### 원인
Zephyr의 `hal_rpi_pico` 모듈이 RP2350/Hazard3 코어 환경에서 빌드될 때, SDK의 include 경로를 외부 애플리케이션에 자동으로 전파하지 못함.
### 해결 시도
*   `target_include_directories`로 경로 추가 -> 실패 (Scope 문제)
*   `zephyr_include_directories`로 경로 추가 -> 실패
*   Kconfig `CONFIG_GPIO_RP2040` 추가 -> 실패 (칩셋 불일치)
### 최종 해결
프로젝트 최상위 `CMakeLists.txt`에서 `ZEPHYR_HAL_RPI_PICO_MODULE_DIR` 변수를 활용해 강제로 경로 주입.
```cmake
zephyr_include_directories(
    ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/rp2_common/hardware_gpio/include
    ...
)
```

## 4. 커스텀 보드 인식 불가
### 증상
Zephyr 워크스페이스에서 `west build` 실행 시:
```text
No board named 'rp2350_w6300_evb' found.
```
### 원인
보드 정의 파일이 Zephyr 트리가 아닌 내 프로젝트(`ethw6300/boards`) 안에 있어서 `west`가 찾지 못함.
### 해결
빌드 명령에 `-DBOARD_ROOT` 옵션 추가하여 내 프로젝트 경로 지정.
```bash
-DBOARD_ROOT=~/workspace/rpi-zephyr/ethw6300
```

## 5. 링커 에러 (Undefined reference)
### 증상
`undefined reference to '__device_dts_ord_54'`
### 원인
Device Tree(DTS) 파일에서는 `spi-bitbang`을 사용하도록 정의했으나, `prj.conf`에서 해당 드라이버를 켜지 않아 실제 구현체가 빌드에서 제외됨.
### 해결
`prj.conf`에 드라이버 활성화:
```properties
CONFIG_SPI_BITBANG=y
CONFIG_GPIO=y
```

---

## 최종 성공 전략 (Best Practice)
외부 프로젝트를 빌드할 때는 **"Zephyr 워크스페이스 안에서 명령을 실행하되, 소스 경로와 보드 경로를 명시해주는 방식"**이 가장 안정적임.

1. `zephyrproject` 폴더로 이동.
2. `west build -s <내_앱_경로> -b <보드명> -- -DBOARD_ROOT=<내_앱_경로>` 실행.
3. 누락된 SDK 헤더는 `CMakeLists.txt`에서 수동으로 `zephyr_include_directories` 처리.
