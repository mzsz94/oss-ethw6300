# W6300-EVB-Pico2 (RP2350) Zephyr 빌드 가이드

이 문서는 구축된 환경에서 W6300-EVB-Pico2 보드용 Zephyr 펌웨어를 빌드하는 방법을 설명합니다.

✦ 현재 프로젝트 구조는 Zephyr의 "Out-of-Tree" (외부 프로젝트) 개발 방식을 따르고 있습니다. 요리에 비유해서 각 폴더의 역할을 설명해 드릴게요.

  🥪 3가지 폴더의 역할 (요리 비유)

   1. `ethw6300/` (나만의 레시피)
       * 역할: 사용자님이 작성한 애플리케이션 코드입니다. (main.c, prj.conf 등)
       * 비유: "내가 만들고 싶은 샌드위치 레시피와 재료(소스 코드)"입니다. Zephyr가 이 레시피를 보고 요리를 합니다.
       * 목표: 이 안의 main.c가 최종 결과물의 핵심 동작이 됩니다.

   2. `zephyrproject/` (주방 & 기본 식재료)
       * 역할: Zephyr OS (운영체제) 커널 및 드라이버 소스 코드입니다.
       * 비유: "공용 주방이자 빵, 햄, 치즈 같은 기본 재료 창고"입니다. 사용자님은 샌드위치 속만 만들고, 빵(커널)과 접시(드라이버)는 여기서 가져다
         씁니다.
       * 특징: west라는 빌드 도구가 이 폴더를 기준으로 동작합니다.

   3. `zephyr-sdk-0.17.0/` (요리 도구)
       * 역할: 컴파일러(Toolchain)입니다. (GCC, Linker 등)
       * 비유: "칼, 오븐, 프라이팬"입니다. 사람이 읽을 수 있는 C언어(레시피)를 기계(보드)가 이해할 수 있는 uf2 파일(완성된 요리)로 변환해 주는 도구
         모음입니다.

  ---

  🔄 main.c가 uf2가 되는 과정

  사용자님이 입력한 빌드 명령어 한 줄이 내부적으로는 이렇게 동작합니다.

   1 west build -s ../ethw6300 ...

   1. 설정 단계: west가 `ethw6300` 폴더의 설정을 읽고, 어떤 재료가 필요한지 파악합니다.
   2. 재료 준비: `zephyrproject`에서 필요한 OS 기능(USB 드라이버, 스케줄러 등)을 가져옵니다.
   3. 요리(컴파일): `zephyr-sdk`에 있는 컴파일러를 불러와서, 사용자님의 main.c와 Zephyr의 OS 코드를 합쳐서 기계어로 번역합니다.
   4. 결과물: 최종적으로 `zephyrproject/build/zephyr/zephyr.uf2` 파일이 생성됩니다.

  📂 결론: 파일은 어디에 생기나요?

  현재 구조에서는 빌드 명령을 zephyrproject 폴더 위치에서 실행하고 있으므로, 결과물은 `zephyrproject` 폴더 안에 생깁니다.

   * 소스 코드: ~/workspace/rpi-zephyr/ethw6300/src/main.c
   * 생성된 파일: ~/workspace/rpi-zephyr/zephyrproject/build/zephyr/zephyr.uf2

  이 zephyr.uf2 파일이 바로 main.c가 컴파일되어 Zephyr OS와 합쳐진 최종 실행 파일입니다. 이것을 보드에 넣으시면 됩니다.

## 1. 사전 준비 (환경 설정)

터미널을 새로 열거나 작업을 시작할 때마다 Python 가상환경을 활성화해야 `west` 명령어를 사용할 수 있습니다.

```bash
# 작업 디렉토리로 이동
cd ~/workspace/rpi-zephyr

# 가상환경 활성화
source zephyrproject/.venv/bin/activate
```

## 2. 프로젝트 구조

현재 커스텀 보드 정의는 Zephyr 원본 소스를 건드리지 않기 위해 별도의 `workspace` 폴더에 분리되어 있습니다.

*   **Zephyr 소스:** `~/workspace/rpi-zephyr/zephyrproject/zephyr`
*   **커스텀 보드 정의:** `~/workspace/rpi-zephyr/workspace/boards/w6300_evb_pico2`
*   **빌드 결과물:** `~/workspace/rpi-zephyr/zephyrproject/build`

## 3. 빌드 명령어

`west build` 명령어를 사용하여 빌드를 수행합니다. 커스텀 보드 위치를 지정하기 위해 `-DBOARD_ROOT` 옵션이 필수적입니다.

### 기본 Blinky 예제 빌드

```bash
# zephyrproject 폴더로 이동 (west 명령어 실행 위치)
cd ~/workspace/rpi-zephyr/zephyrproject

# 빌드 실행
west build -p always \
  -b w6300_evb_pico2/rp2350a/m33 \
  zephyr/samples/basic/blinky \
  -- -DBOARD_ROOT=$HOME/workspace/rpi-zephyr/workspace
```

**옵션 설명:**
*   `-p always`: 이전 빌드 아티팩트를 삭제하고 깨끗한 상태에서 다시 빌드합니다 (Pristine build).
*   `-b w6300_evb_pico2/rp2350a/m33`: 빌드할 보드와 타겟(SoC/Core)을 지정합니다.
*   `zephyr/samples/basic/blinky`: 빌드할 애플리케이션의 소스 경로입니다.
*   `-- -DBOARD_ROOT=...`: **중요!** 커스텀 보드 파일(`w6300_evb_pico2`)이 있는 상위 디렉토리를 CMake에 알려줍니다.

## 4. 빌드 결과물 확인

빌드가 성공적으로 완료되면 아래 경로에 펌웨어 파일이 생성됩니다.

*   **UF2 파일 (플래싱용):** `build/zephyr/zephyr.uf2`
*   **ELF 파일 (디버깅용):** `build/zephyr/zephyr.elf`

## 5. 보드에 업로드 (Flashing)

1.  W6300-EVB-Pico2 보드의 **BOOTSEL 버튼**을 누른 상태로 USB 케이블을 PC에 연결합니다.
2.  PC에 `RP2350`이라는 이름의 USB 드라이브(Mass Storage)가 인식됩니다.
3.  생성된 `zephyr.uf2` 파일을 해당 드라이브로 복사(또는 드래그 앤 드롭)합니다.
4.  복사가 완료되면 보드가 자동으로 재부팅되며 LED가 깜빡이기 시작합니다.

## 6. 트러블슈팅

*   **`west: command not found` 오류:**
    *   1번 항목의 가상환경 활성화(`source .../activate`)를 수행했는지 확인하세요.
*   **`Board w6300_evb_pico2 not found` 오류:**
    *   명령어 끝에 `-DBOARD_ROOT` 경로가 정확한지 확인하세요. 절대 경로(`$HOME` 사용)를 권장합니다.


pwd ~/workspace/rpi-zephyr cd zephyrproject && ../ethw6300/.venv/bin/west build -p always -s ../ethw6300 -b rp2350_w6300_evb/rp2350a/hazard3 -- -DBOARD_ROOT=/home/suzinee_u/workspace/rpi-zephyr/ethw6300 