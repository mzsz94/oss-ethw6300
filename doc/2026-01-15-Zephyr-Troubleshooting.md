# Zephyr RP2350 빌드 및 USB 콘솔 문제 해결 로그
**날짜:** 2026년 1월 15일  
**프로젝트:** `ethw6300` (Wiznet W6300 Driver Test on RP2350/Pico2)  
**환경:** Linux (Build Host), macOS (Serial Monitor) 

오늘 진행한 작업은 RP2350 보드에서 Zephyr를 빌드하고, macOS에서 USB 시리얼(CDC ACM)을 통해 로그를 확인하는 과정에서 발생한 문제들을 해결하는 것이었습니다.

---

## 1. 빌드 타겟 지정 오류 (Board Qualifiers)

### 문제 상황
`west build -b w6300_evb_pico2 ...` 명령 실행 시 보드 타겟을 찾을 수 없다는 에러 발생.

### 원인
RP2350(Pico 2)부터는 동일한 보드에서도 코어(Core) 아키텍처가 나뉘므로(Arm Cortex-M33 vs RISC-V Hazard3), 구체적인 **Qualifier**를 명시해야 합니다.

### 해결 방법
보드 이름 뒤에 `/rp2350a/m33`를 추가하여 Arm 코어로 빌드하도록 수정했습니다.

```bash
# 수정 전
west build -b w6300_evb_pico2 ...

# 수정 후 (성공)
west build -b w6300_evb_pico2/rp2350a/m33 ...
```

---

## 2. USB 스택 호환성 문제 (macOS 인식 불가)

### 문제 상황
빌드는 성공했으나 macOS(`ls /dev/tty.*`)에서 장치가 인식되지 않음. `prj.conf`에서 Zephyr의 차세대 USB 스택(`CONFIG_USB_DEVICE_STACK_NEXT`)을 사용하려 했으나 설정이 복잡하고 의존성 경고가 다수 발생함.

### 원인
새로운 USB 스택은 아직 초기 설정이 복잡하며, 단순한 `printk`/`LOG_INF` 출력용으로는 **기존(Legacy) USB 스택**이 훨씬 안정적이고 설정이 간편합니다.

### 해결 방법 (`prj.conf`)
`NEXT` 스택 관련 설정을 제거하고, 검증된 `Legacy` 스택으로 복귀했습니다.

```properties
# [삭제] 새로운 스택 설정 (불안정/설정 복잡)
# CONFIG_USB_DEVICE_STACK_NEXT=y
# CONFIG_UDC_DRIVER=y

# [추가] 기존 레거시 스택 설정 (안정적)
CONFIG_USB_DEVICE_STACK=y
CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=n  # 코드에서 직접 제어하기 위해 n 설정
CONFIG_USB_DC_RPI_PICO=y

# CDC ACM (시리얼) 설정
CONFIG_USB_CDC_ACM=y
CONFIG_UART_LINE_CTRL=y  # DTR 신호 확인을 위해 필수
```

---

## 3. 부팅 로그 유실 (DTR Wait 추가)

### 문제 상황
장치는 인식되지만, 터미널(screen/minicom)을 연결했을 때는 이미 부팅 메시지("Hello...")가 지나가버려 아무것도 보이지 않음.

### 원인
Zephyr OS의 부팅 속도가 USB 시리얼 연결 속도보다 빠르기 때문에, PC에서 터미널을 열기 전에 `main()` 함수가 실행되어 로그를 출력해버림.

### 해결 방법 (`src/main.c`)
USB 연결 후, 사용자가 터미널을 열어 **DTR(Data Terminal Ready)** 신호를 보낼 때까지 대기하는 코드를 추가했습니다.

```c
#include <zephyr/drivers/uart.h> 
#include <zephyr/usb/usb_device.h>

int main(void)
{
    const struct device *const dev = DEVICE_DT_GET_ANY(zephyr_cdc_acm_uart);
    uint32_t dtr = 0;

    // 1. USB 스택 활성화
    if (usb_enable(NULL)) {
        return 0;
    }

    // 2. PC에서 터미널을 열 때까지 대기 (DTR 신호 폴링)
    while (!dtr) {
        uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
        k_sleep(K_MSEC(100));
    }

    // 3. 연결 확인 후 로그 출력 시작
    while (1) {
        LOG_INF("Hello from W6300 Driver Test App!");
        k_sleep(K_SECONDS(1));
    }
    return 0;
}
```

---

# 최종 빌드 명령어 (Reference)

Zephyr 워크스페이스(`zephyrproject`) 폴더 내에서 실행해야 합니다.

### Arm Cortex-M33 코어 타겟
```bash
west build -p always -b w6300_evb_pico2/rp2350a/m33 ../ethw6300 -- -DBOARD_ROOT=/home/suzinee_u/workspace/rpi-zephyr/workspace
```

### RISC-V Hazard3 코어 타겟
```bash
west build -p always -b w6300_evb_pico2/rp2350a/hazard3 ../ethw6300 -- -DBOARD_ROOT=/home/suzinee_u/workspace/rpi-zephyr/workspace
```

```bash
cd zephyrproject && source .venv/bin/activate && west build -p always -b w6300_evb_pico2/rp2350a/hazard3 ../ethw6300 -- -DBOARD_ROOT=/home/suzinee_u/workspace/rpi-zephyr/workspace
```

---

## 5. RISC-V Hazard3 지원

RP2350의 특징인 듀얼 아키텍처 지원에 따라, 동일한 소스 코드를 RISC-V 코어로도 빌드할 수 있습니다. 

- **호환성:** 위에서 설정한 Legacy USB 스택 및 DTR 대기 로직은 아키텍처에 독립적이므로 RISC-V에서도 수정 없이 동일하게 동작합니다.
- **툴체인:** `west`가 자동으로 `riscv64-zephyr-elf` 툴체인을 찾아 빌드를 수행합니다.

