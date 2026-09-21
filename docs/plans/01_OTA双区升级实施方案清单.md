# 01_OTA双区升级实施方案清单

> **目标**：实现基于 SPI-Flash (W25Q64) 的高可靠双区差错容灾 Bootloader + FreeRTOS APP 在线升级系统。  
> **当前状态**：`Phase 1 ~ Phase 3` 已完成；`Phase 4` 验证准备中。

---

## 一、方案设计与物理约束 (Spec & Blueprint)

### 1. 核心业务流程
1. **APP 运行态**：通过串口 DMA 接收新固件及末尾 4 字节 CRC32，校验无误后写入 W25Q64 新固件区，写入 `BootMeta` (状态标记为 `UPGRADE_PENDING`)，执行软复位 `NVIC_SystemReset()`。
2. **Bootloader 引导态**：
   - 检查 `BootMeta` 标记：
     - 若为 `PENDING`：备份片内当前运行的 APP 到 W25Q64 备份区，将新固件从片外 Flash 烧写到片内 `0x08002000`，更新状态为 `TESTING`，重试计数清零，跳转进新 APP。
     - 若为 `TESTING` 且连续重启异常（重试计数超过 3 次）：判定新固件异常，自动从备份区恢复旧固件，回滚状态。
     - 若为 `IDLE`：执行合法性检查（栈顶在 RAM 范围、复位中断向量在 Flash 范围），直接跳转进 APP。
3. **新 APP 确认转正**：APP 启动并完成外设自检后调用 `App_Confirm_Healthy()`，将状态转正为 `IDLE`。

---

## 二、分阶段实施清单 (Implementation Checklist)

### Phase 1: 片外 Flash 存储布局与通信驱动 (已完成)
- [x] 配置 W25Q64 驱动及扇区擦除、页写入、跨页读取接口。 (`BSP/W25QXX/`)
- [x] 定义统一的 `BootMeta_t` 元数据结构体与 Magic 校验 (`0x5AA51234`)。 (`BSP/CRC32/boot_config.h`)
- [x] 实现标准 IEEE 802.3 CRC32 查表法校验模块。 (`BSP/CRC32/crc32.c`)
- **验收结果**：底层驱动编译通过，CRC32 与 zlib 完全对齐。

### Phase 2: APP 端固件接收、校验与元数据落盘 (已完成)
- [x] 串口空闲中断 + DMA 动态固件流接收。 (`BSP/USART/usart.c`)
- [x] 创建 `iap_task` 信号量驱动处理任务。 (`APP_TASK/app_tasks.c`)
- [x] 提取末尾 4 字节小端 CRC 进行整体验证，拦截残损包。
- [x] 写入 W25Q64 暂存区并写入 `PENDING` 升级标记后触发系统软复位。
- **验收结果**：Keil 编译 0 Error, 0 Warning。 (Commit: `37222f3`)

### Phase 3: Bootloader 底座与双区 Flash 隔离适配 (已完成)
- [x] 独立 Bootloader 工程配置（Flash 起始 `0x08000000`，大小 8KB）。 (`Bootloader/bootloader.uvprojx`)
- [x] APP 工程基址迁移配置（Flash 起始 `0x08002000`，大小 56KB）。 (`USER/project.uvprojx`)
- [x] APP `main()` 入口配置中断向量表偏移 `SCB->VTOR = FLASH_BASE | 0x2000;`。 (`USER/main.c`)
- [x] APP 初始化流程接入 `App_Confirm_Healthy()` 闭环自检转正。 (`USER/main.c`)
- **验收结果**：双工程编译全绿（Bootloader: 2.7KB, APP: 44.8KB）。 (Commit: `fd4cc8d`)

### Phase 4: 上位机打包工具与全链路烧录测试 (进行中)
- [x] 验证 `tools/add_crc32.py` 生成符合硬件协议的小端 CRC32 固件包。
- [ ] **任务 4.1**: 烧录 Bootloader (`0x08000000`) 与初始 APP 固件 (`0x08002000`)。
- [ ] **任务 4.2**: 制作新版本固件包并通过串口发送，观察 LCD 升级交互提示。
- [ ] **任务 4.3**: 验证升级成功后自动跳入新系统并完成 `IDLE` 状态转正。
- [ ] **任务 4.4**: 模拟断电/异常固件，验证 Bootloader 自动回滚容灾机制。
- **验收标准**：全链路无需外部调试器介入，完成串口平滑升级与异常回滚。
