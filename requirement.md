목표: RISC-V Ethernet Driver 기여
개발환경: 
E6300-EVB-PICO2
RP2350: Zephyr samples/net/http_server
Android:Chrome 접속 or 앱

최종 목표 구조
drivers/ethernet/
 ├── eth_w6300.c        <- 새로 만들 파일
 ├── eth_w6300.h
 └── CMakeLists.txt

dts/
 └── bindings/ethernet/
      └── wiznet,w6300.yaml

boards/arm|riscv/rp2350_w6300_evb/
 ├── board.dts
 ├── board.yaml
 └── Kconfig.board

Zephyr Driver Architecture
+----------------------+
| Zephyr net stack     |
+----------------------+
           ↑
   struct net_if
           ↑
+----------------------+
| eth_driver_api       | ← send(), set_config(), get_capabilities()
+----------------------+
           ↑
+----------------------+
| eth_w6300.c          | ← SPI + IRQ + W6300 registers
+----------------------+
           ↑
| SPI driver | GPIO irq|


첫 번째 PR 목표 (1~2주)
“Zephyr에서 W6300 Ethernet 디바이스 인식되게 하기”