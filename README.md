# 基于 FreeRTOS + STM32 的烟机控制系统

当前工程：STM32F103C8T6、标准外设库、FreeRTOS、W25Q64、USART1 串口 IAP v3。

- APP 工程：`USER/project.uvprojx`，运行地址 `0x08002000`，最大 56KB。
- Bootloader 工程：`Bootloader/bootloader.uvprojx`，运行地址 `0x08000000`，最大 8KB。
- 当前整改、接线、首次烧录及测试说明：[升级可靠性整改](docs/plans/04_升级可靠性整改.md)。
- 历史方案 `01`～`03` 保留研发过程；与当前代码冲突时以 `04` 及 Shared 配置为准。

## 本次接线与兼容性变化

**按键2由 PB12 改接 PB0，Flash CS 仍为 PB12。必须实际改线。**

IAP 协议和元数据升级为 v3，不能混用旧版 APP、Bootloader、发送工具。
首次安装需要用 SWD 同时烧录两个新工程，按住 KEY1（PB1）上电登记已烧录的 APP；
旧 v2 元数据不会自动迁移。此后串口升级不需要按键授权，黄金镜像首次固化需要维护按键授权。

## 构建与验证

```powershell
.\tools\build_keil.ps1
python .\tools\host_tests\run_iap_selftest.py
```

构建脚本可通过 `-KeilRoot` 指定 Keil 安装目录，输出 BIN/HEX 至 OBJ 和 OBJ_BL。
离线测试使用实际共享 C 状态机、协议解析及升级服务，模拟硬件存储/时间/复位；不能代替上板验证。

## 片外 Flash 分区

| 起始地址 | 区域 | 容量 |
|---|---|---|
| `0x000000` | 元数据 A | 4KB |
| `0x001000` | 元数据 B | 4KB |
| `0x002000` | 下载区 | 56KB |
| `0x010000` | 上一稳定版备份 | 64KB |
| `0x020000` | 黄金镜像 | 64KB |
| `0x030000` | 预留 | 其余空间 |

发布时必须配置私有密钥。默认开发密钥公开，仅用于实验联调；尚未配置读保护或硬件防回退计数器。
