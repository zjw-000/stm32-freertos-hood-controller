# 油烟机控制系统技术实现文档

> 本文是对当前工程源码与随附构建产物的实现回忆文档，目标是帮助接手者快速恢复架构、控制链路、升级边界和复现路径。文中“已核实”只表示已从文件内容或目录中确认；没有把仅存在于注释、命名或推测中的内容写成运行结论。

- 审计日期：2026-08-23
- 工程根目录：`D:/project/GenericAgent/temp/range_hood_source_audit/range_hood_project/range_hood_project`
- 目标平台：源码注释标明 `STM32F103C8T6 + FreeRTOS`
- 主要语言：C；配套 Keil/ARM 工程产物痕迹
- 文档状态：源码级恢复记录，未宣称本次已完成硬件烧录、运行或重新编译

## 1. 一页摘要

这是一个基于 STM32F1、FreeRTOS 和多任务应用层的油烟机控制系统。系统把电机控制拆成“硬件驱动 + 速度反馈 + PID 速度环 + 传感器融合 + 模式/策略任务 + UI/IAP”几个边界：

1. `USER/main.c` 完成硬件初始化、系统状态初始化、任务创建并启动调度器。
2. `APP_TASK/app_tasks.c` 维护全局 `g_systemState`，创建按键、传感器、风速计算、电机控制、UI、防回流及可选 IAP 任务。
3. `BSP/WIND/wind_speed.c` 将温度、湿度、气体浓度归一化，加权融合为 20% 到 100% 的 PWM 建议值，并判定 Cooking Event。
4. `MotorControlTask` 在待机、手动、自动、防回流四种模式下决定电机启停与 PWM；手动模式使用目标 RPM + PID，自动模式使用风速算法结果。
5. 编码器由 TIM2 计数，TIM2 更新中断维护溢出计数，TIM4 更新中断通过二值信号量唤醒 `SpeedCalcTask`，测速结果写入全局 `speed`。
6. IAP 代码由 `ifopen` 条件编译控制，DMA1 Channel5 接收 USART1 数据，DMA 传输完成中断唤醒 `iap_task`；固件长度、CRC、Flash 地址和 boot/app 容量仍需结合完整启动工程确认。

## 2. 代码与产物地图

以下路径均相对于工程根目录：

| 区域 | 文件/目录 | 责任 | 已核实重点 |
|---|---|---|---|
| 入口 | `USER/main.c` | 启动顺序与硬件初始化 | `Hardware_Init()` -> `System_Init()` -> `StartTask_Create()` -> `vTaskStartScheduler()` |
| 入口接口 | `USER/main.h` | 全局对象和系统初始化声明 | 包含 FreeRTOS、PID、风速、IAP 等依赖接口 |
| 应用头文件 | `APP_TASK/app_tasks.h` | 状态模型、时序参数、任务配置、API | 四模式、优先级、栈大小、`SystemState_t` |
| 应用实现 | `APP_TASK/app_tasks.c` | 状态机、任务、ISR、模式切换 | 控制闭环和升级任务主体 |
| 风速算法 | `BSP/WIND/wind_speed.h/.c` | 传感器归一化、加权融合、Cooking Event | 温度/湿度/气体权重为 0.2/0.2/0.6 |
| 电机驱动 | `BSP/MOTOR/motor.c` | TIM1 PWM、H 桥使能、方向、停止/启动 | TIM1 CH1/CH1N，PA2 为 H 桥使能 |
| RTOS 配置 | `FreeRTOS/include/FreeRTOSConfig.h` | FreeRTOS 编译配置 | 需结合完整文件和工程选项确认最终编译宏 |
| 库/板级目录 | `BSP/`、`SYSTEM/`、`MIDDLEWARE/`、`FreeRTOS/` | 外设、系统工具、中间件和内核 | 具体依赖由工程文件和 include 路径最终决定 |
| 构建产物 | `OBJ/PWM.axf` | ARM ELF/调试或链接产物 | 文件存在；未在本次审计中执行反汇编/符号核验 |
| 构建产物 | `USER/PWM.map` | 链接映射文件 | 文件存在；未在本次审计中提取内存布局结论 |
| 辅助资料 | `APPV1.1/`（若作为同级版本目录存在） | 旧版启动/占位 APP 对照 | 旧版 `USER/main.c` 只跳转到 `0x0800F000` 后 LED 闪烁 |

本次未发现工程根已有 `TECH_PAPER.md`，本文为新建文件。目录中已通过文件名搜索确认存在 `OBJ/PWM.axf` 和 `USER/PWM.map`；未确认 `.hex`、`.bin`、`.uvprojx` 或完整 bootloader 工程是否存在。

## 3. 依赖与启动顺序

### 3.1 软件依赖

源码直接或间接依赖以下组件：

- STM32F10x 标准外设库风格接口：`NVIC_PriorityGroupConfig`、`TIM_*`、`DMA_*`、GPIO/RCC 等。
- FreeRTOS：任务、队列/信号量头文件以及 `xTaskCreate`、`vTaskStartScheduler`、`xSemaphoreTake/Give`、ISR 版本 API。
- 电机/PWM 驱动：`TIM1_dead_pwm_init`、`motor_init`、`motor_start`、`motor_stop`、`motor_pwm_set` 或其底层封装。
- 编码器和测速：`TIM2_encode_init`、`get_encoder_value`、`get_speed`、`TIM_GetDirection`。
- 传感器：MQ2，以及 DHT11 结构/接口（系统状态包含温度和湿度字段）。
- UI：LCD 初始化、清屏、字符串显示接口。
- PID：`PID_Init`、`PID_SetTarget`、`PID_Calculate`。
- 串口/DMA/IAP：USART1、DMA1 Channel5、`MYDMA_Config`、IAP 读写/校验接口。

`FreeRTOSConfig.h`、启动文件、链接脚本/Keil target 设置和完整的第三方库版本决定最终可编译性。本文件不把目录名当作已经验证的编译依赖版本。

### 3.2 启动顺序

`USER/main.c` 的可见顺序如下：

```text
main
  -> Hardware_Init
       -> NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4)
       -> delay_init
       -> TIM1_dead_pwm_init(1000-1, 72-1, 0, 100)
       -> motor_init
       -> TIM2_encode_init(0xFFFF, 0)
       -> Beep_Init(PB15)
       -> Key_Init
       -> MQ2_Init
       -> PID_Init(Kp=14.0, Ki=1.65, Kd=0.0, output=0..1000)
       -> WindSpeed_Init
       -> LCD_Init
       -> uart_init(115200)
       -> if ifopen: MYDMA_Config(DMA1_Channel5, USART1->DR, receive_buff, buff_size)
  -> System_Init
       -> 创建 g_dataMutex
       -> 创建 g_speedCalcSemaphore
       -> if ifopen: 创建 g_iapSemaphore
  -> StartTask_Create
       -> 创建各 FreeRTOS 任务
  -> vTaskStartScheduler
```

初始化完成后 LCD 显示 `Init Complete! ` 并延时 500 ms。调度器正常接管后，`main` 末尾的空 `while(1)` 按源码设计不应成为业务循环。

### 3.3 中断优先级约束

`USER/main.c` 的注释说明使用 `NVIC_PriorityGroup_4`，并要求调用 FreeRTOS ISR API 的中断优先级不高于允许阈值；注释中举例 DMA、TIM4、TIM2 的优先级分别为 4、5、6。实际优先级寄存器配置分布在各定时器/DMA初始化文件中，接手时应逐项核对，不能只依据注释。

## 4. 状态模型与共享数据

### 4.1 工作模式

`APP_TASK/app_tasks.h` 定义四个模式，循环顺序由 `System_SwitchMode()` 实现：

```text
MODE_STANDBY -> MODE_MANUAL -> MODE_AUTO -> MODE_ANTI_BACKFLOW -> MODE_STANDBY
```

| 模式 | 主要控制来源 | 电机行为 |
|---|---|---|
| `MODE_STANDBY` | `MotorControlTask` | `motor_stop()`，电机停止，但速度计算仍运行 |
| `MODE_MANUAL` | 档位 + PID | LOW/HIGH 映射目标 RPM，再由速度环调 PWM |
| `MODE_AUTO` | 自动状态机 + Cooking Event + 风速融合 | 启动阶段低速运行，Cooking Event 时使用算法 PWM，无事件后延时关闭 |
| `MODE_ANTI_BACKFLOW` | `AntiBackflowTask` | 气体浓度越过阈值时启动，浓度回落后停止 |

切换到自动模式时会将 `g_autoModeState` 置为 `AUTO_STATE_STARTUP`。切换到非自动模式时，`System_SwitchMode()` 会停止电机并清除 `motorRunning`。

### 4.2 `SystemState_t`

系统状态结构体包含：

- 模式与档位：`currentMode`、`speedLevel`。
- 电机状态：`motorRunning`。
- 传感器值：`temperature`、`humidity`、`gasConcentration`。
- 控制量：`windSpeedPWM`、`actualRPM`、`targetRPM`。
- 策略状态：`cookingEventActive`、`antiBackflowActive`、`gasThreshold`。
- 计时器：`autoModeCounter`、`cookingEventCounter`。

`g_dataMutex` 用于保护部分状态读写。当前实现中并非每一个任务访问都明显持锁，接手时应把 `g_systemState`、`speed`、`overflow`、算法全局结构体的并发访问作为重点检查项。

### 4.3 时序常量

| 宏 | 值 | 语义 |
|---|---:|---|
| `AUTO_MODE_STARTUP_TIME` | 60000 ms | 自动模式启动等待时间 |
| `COOKING_EVENT_TIMEOUT` | 60000 ms | Cooking Event 超时计时 |
| `COOKING_EVENT_DELAY_OFF` | 10000 ms | Cooking Event 消失后的延时关闭 |

任务实现使用 `delay_ms()` 作为周期等待，而不是统一使用 `vTaskDelay()`。这意味着 `delay_ms()` 是否让出 CPU、是否依赖 RTOS tick，必须回到其实现确认；这是运行时验证前的重要边界。

## 5. 任务与同步关系

### 5.1 任务清单

`APP_TASK/app_tasks.h` 给出的优先级和栈大小如下。优先级数字越大是否代表越高由 FreeRTOS 配置语义决定，源码注释将 `SpeedCalcTask` 描述为最高优先级。

| 任务 | 函数 | 周期/触发 | 优先级 | 栈大小 |
|---|---|---|---:|---:|
| 开始任务 | `StartTask` | 启动阶段 | 1 | 64 |
| 按键任务 | `KeyScanTask` | 10 ms 扫描 | 4 | 64 |
| 传感器任务 | `SensorTask` | 500 ms 采集 | 3 | 128 |
| 电机控制任务 | `MotorControlTask` | 50 ms 控制 | 5 | 256 |
| UI 任务 | `UIDisplayTask` | 200 ms 显示（以函数注释为准） | 1 | 256 |
| 防回流任务 | `AntiBackflowTask` | 100 ms 检测 | 2 | 64 |
| 风速计算任务 | `WindSpeedTask` | 100 ms 算法更新 | 3 | 64 |
| 速度计算任务 | `SpeedCalcTask` | TIM4 ISR 信号量触发 | 6 | 128 |
| IAP 任务 | `iap_task` | DMA 完成信号量触发；`ifopen` 开启 | 7 | 256 |

头文件还声明了 `StartTask_Create()`、模式切换 API 和 `System_GetState()`。IAP 任务是否实际创建取决于 `ifopen` 宏，而不是仅取决于函数是否存在。

### 5.2 同步对象

已核实的对象：

- `g_dataMutex`：系统状态互斥信号量。
- `g_speedCalcSemaphore`：TIM4 更新 ISR -> `SpeedCalcTask` 的二值信号量。
- `g_iapSemaphore`：DMA 完成 ISR -> `iap_task` 的二值信号量，受 `ifopen` 控制。
- `xSemaphoreGiveFromISR()` 后调用 `portYIELD_FROM_ISR()`，用于在需要时立即切换到唤醒任务。

源码还包含队列头文件和相关 RTOS 依赖，但本次已读范围中未确认某个业务队列实际参与主控制闭环，因此不能把队列写成必经链路。

## 6. 传感器与风速算法

### 6.1 输入与参数

`BSP/WIND/wind_speed.h` 定义：

- 温度基线/上限：20 / 35 摄氏度。
- 湿度基线/上限：40 / 75 %。
- 气体基线/上限：80 / 450。
- 权重：温度 0.2，湿度 0.2，气体 0.6。
- PWM 输出范围：20.0% 到 100.0%。
- Cooking Event 条件：`temp > 26 && humidity > 50 && gas > 100.0f`。
- 手动目标转速：LOW 190 RPM，HIGH 220 RPM。

### 6.2 计算流程

`WindSpeed_Update(temp, humidity, gas)` 先做三路归一化并分别限制到 `[0, 1]`：

```text
f_T = constrain((T - 20) / (35 - 20), 0, 1)
f_H = constrain((H - 40) / (75 - 40), 0, 1)
f_G = constrain((G - 80) / (450 - 80), 0, 1)

F = 0.2 * f_T + 0.2 * f_H + 0.6 * f_G
PWM_percent = constrain(20 + (100 - 20) * F, 20, 100)
```

随后按严格大于条件判定 Cooking Event。边界值 26、50、100.0 不会触发对应的“大于”判断；这是联调时应使用的边界测试点。

`WindSpeed_GetPWMCompare(maxCompare)` 将百分比 PWM 转成定时器比较值；函数实现和取整方式应在需要精确波形时直接核对。当前工程使用 `MAXCCR=1000` 的约定，自动控制路径将比较值传给电机 PWM 接口。

### 6.3 采集与状态更新

`SensorTask` 负责周期性获取温度、湿度、气体数据，并调用风速算法更新；随后在取得 `g_dataMutex` 后将算法 PWM 和 Cooking Event 标志回写到 `g_systemState`。`WindSpeedTask` 以 100 ms 周期计算/更新算法相关状态。由于任务间共享算法全局结构体，若要证明无竞态，需要结合完整实现和实际调度进行验证。

## 7. 电机控制闭环

### 7.1 硬件输出

`Hardware_Init()` 调用：

```c
TIM1_dead_pwm_init(1000 - 1, 72 - 1, 0, 100);
motor_init();
```

源码注释给出的 TIM1 PWM 频率是 1 kHz。`BSP/MOTOR/motor.c` 显示：

- TIM1 CH1 使用 PA8。
- TIM1 互补 CH1N 使用 PB13。
- PB12 为刹车输入，上拉输入。
- PA2 用作 H 桥驱动使能。
- `motor_stop()` 关闭 CH1/CH1N 并拉低 PA2。
- `motor_start()` 拉高 PA2。
- `motor_dir(stright)` 配置正转路径。
- `motor_speed(ccr)` 对比较值做 `<=1000` 限幅后写 TIM1 CCR1。

`motor_init()` 先设正转并停止，再启动驱动使能；启动后的实际 PWM 输出是否仍关闭取决于 TIM1 通道初始化状态和后续控制任务，需在板上测量确认。

### 7.2 手动模式

在 `MotorControlTask` 的 `MODE_MANUAL` 分支中，若 `motorRunning` 为真：

1. 由 `WindSpeed_GetTargetRPM(speedLevel)` 取得 LOW/HIGH 目标 RPM。
2. 通过 `PID_SetTarget(&g_speedPID, targetRPM)` 设置目标。
3. 以全局 `speed` 为实际速度输入 `PID_Calculate(&g_speedPID, speed)`。
4. 将 PID 输出传给 `motor_pwm_set()`。

PID 在 `Hardware_Init()` 中初始化为 `Kp=14.0`、`Ki=1.65`、`Kd=0.0`，输出范围 `0..1000`。实际 PID 离散周期、积分抗饱和和 `motor_pwm_set` 到 CCR 的映射要以对应实现为准。

### 7.3 自动模式状态机

自动状态由 `g_autoModeState` 表示，名称数组包含 `Startup`、`Cooking`、`DelayOff` 三态。

```text
AUTO_STATE_STARTUP
  -> motorRunning=1, motor_start()
  -> motor_pwm_set(PWM_MIN * 10)
  -> 等待 Cooking Event 或启动计时条件

AUTO_STATE_COOKING
  -> 读取 WindSpeed_GetPWMCompare(MAXCCR)
  -> motor_pwm_set(pwmCompare)
  -> 继续检测 Cooking Event

AUTO_STATE_DELAY_OFF
  -> 使用延时关闭逻辑
  -> 再次检测到 Cooking Event: 回到 COOKING
  -> cookingEventCounter >= 10000 ms: MODE_STANDBY、停止电机
```

源码分支确认了 DelayOff 状态在每个 50 ms 控制周期累加 `cookingEventCounter += 50`，再次检测到事件会回到 Cooking 并清零计数；超过 10 秒则切换待机并停止电机。启动阶段的 60 秒常量和具体状态切换条件需结合 `MotorControlTask` 未截断全文再次核对，不能只从宏名推定行为。

### 7.4 防回流模式

`AntiBackflowTask` 每 100 ms 执行：

1. 仅当 `currentMode == MODE_ANTI_BACKFLOW` 时工作。
2. 以 `gasConcentration >= gasThreshold` 判定检测到事件。
3. 事件发生且气体浓度仍低于 `GAS_THRESHOLD_HIGH` 时启动电机、置 `antiBackflowActive=1`，并把当前阈值切换到 HIGH，避免重复触发。
4. 未检测到事件且气体浓度低于 `GAS_THRESHOLD_NORMAL` 时停止电机、清除激活标志，并把阈值恢复为 NORMAL。
5. 非防回流模式下清除激活标志并重置到 NORMAL 阈值。

`GAS_THRESHOLD_NORMAL`、`GAS_THRESHOLD_HIGH` 的具体数值应从 `app_tasks.c` 定义处和传感器标定资料确认；当前可确认的是双阈值/滞回意图和 HIGH 上限保护分支。

## 8. 按键、UI 与用户操作

源码注释定义：

- 按键 1：PB1，短按切换工作模式。
- 按键 2：PB12，短按切换档位；长按持续鸣叫；释放时停止蜂鸣。

`KeyScanTask` 以 10 ms 扫描周期读取按键事件，并处理 `KEY_EVENT_SHORT_PRESS`、`KEY_EVENT_LONG_PRESSING`、`KEY_EVENT_RELEASE` 等事件。模式/档位/电机切换函数使用 `g_dataMutex` 保护状态。

UI 任务使用 LCD 清屏、显示状态字符串和传感器/控制数据。初始化阶段会显示 `Init Complete! `；手动启停接口还会显示 `Motor Started` 或 `Motor Stopped`。显示内容和刷新周期以 `UIDisplayTask` 当前实现为准，不能把调试字符串当作稳定产品 UI 协议。

## 9. 速度反馈链路

### 9.1 硬件与采样

`Hardware_Init()` 调用 `TIM2_encode_init(0xFFFF, 0)` 初始化编码器计数。`TIM2_IRQHandler()` 在更新事件发生时清除标志，并依据 `TIM_GetDirection(TIM2)` 对全局 `overflow` 做递增或递减处理。

TIM4 更新中断执行：

```text
清 TIM4 更新标志
 -> xSemaphoreGiveFromISR(g_speedCalcSemaphore, ...)
 -> portYIELD_FROM_ISR(...)
```

`SpeedCalcTask` 阻塞等待该二值信号量，收到后调用：

```c
encoderCount = get_encoder_value();
speed = get_speed(encoderCount, 50);
```

源码注释将其定义为每 50 ms 计算一次。`MotorControlTask` 随后将全局 `speed` 作为 PID 实际转速输入。

### 9.2 接手时重点

- 核对 TIM4 的真实更新频率是否确为 50 ms。
- 核对 `get_encoder_value()` 是否原子地合并计数值和 `overflow`，否则 TIM2 ISR 与任务读取可能产生撕裂。
- 核对 RPM 换算所需的编码器线数、倍频方式、减速比和方向约定。
- 核对 `actualRPM` 是否持续回写 `g_systemState`；头文件有该字段，但不能仅据字段存在断言所有路径都更新。

## 10. IAP / Boot / App 边界

### 10.1 已核实的应用侧行为

`app_tasks.c` 中 IAP 任务及 DMA ISR 在 `#if ifopen` 条件下编译：

- `System_Init()` 创建 `g_iapSemaphore`。
- `StartTask_Create()` 创建 `iap_task`，任务优先级 7、栈 256。
- DMA1 Channel5 传输完成中断清除 `DMA1_IT_TC5` 标志，并释放 `g_iapSemaphore`。
- `iap_task` 阻塞等待该信号量，收到后读取 DMA 接收长度，并进入固件处理流程。
- `USER/main.c` 在 `ifopen` 打开时将 USART1 数据寄存器作为 DMA 源，目标为 `receive_buff`，长度为 `buff_size`。

主入口注释说明系统意图包括 USART + DMA 接收固件、接收完成后跳转 APP、boot + app 双区架构以及 CRC32。另有 Python 脚本预处理 `app.bin` 并把 CRC32 写入固件的说明，但本次未确认该脚本、协议格式和完整 bootloader 源码。

### 10.2 地址与容量的证据边界

已核实新版入口包含 `SCB->VTOR = FLASH_BASE | 0xF000` 这一类向量表重定位配置；旧版 `APPV1.1/USER/main.c` 的可见逻辑为跳转到 `0x0800F000` 后 LED 闪烁。两者支持“APP 起始地址意图为 Flash 偏移 `0xF000`”这一判断。

但是源码注释中的 IAP 地址、boot/app 分区容量描述存在矛盾，不能据此直接生成量产烧录地址或升级包布局。以下项目必须在完整工程中确认后才能用于升级：

- bootloader 的实际链接地址和向量表地址。
- APP 的实际起始地址，是否始终为 `0x0800F000`。
- boot 区、APP 区和暂存区的结束地址及 Flash 页边界。
- CRC32 存储位置、覆盖范围、字节序和校验失败策略。
- DMA 接收协议、固件长度字段、超时、断电恢复和回滚策略。
- `ifopen` 的最终值、IAP 任务是否在量产配置中启用。

因此本文只记录“源码可见的升级链路”，不提供未经确认的 `.bin` 偏移、Flash 擦写范围或升级命令作为操作指令。

## 11. 两个版本的关系

当前审计目录中存在新版工程和 `APPV1.1` 旧版对照内容：

| 对比项 | 新版工程 | `APPV1.1` 旧版 |
|---|---|---|
| 启动主流程 | 硬件初始化、系统初始化、创建任务、启动 FreeRTOS | 可见代码跳转到 `0x0800F000`，随后 LED 闪烁 |
| 控制功能 | 四模式、传感器融合、PID、编码器测速、UI、可选 IAP | 未见完整控制闭环 |
| 向量表/APP 意图 | `VTOR` 配置为 Flash 偏移 `0xF000` | 直接跳转到 `0x0800F000` |
| 可复用结论 | 以新版源码为主，旧版只作启动地址/占位行为参照 | 不应当作新版功能实现依据 |

版本目录的具体构建关系、链接地址和发布用途尚未由工程文件完整证明。接手者不要把旧版 LED 占位 APP 当作可运行的完整产品固件。

## 12. 构建与复现路径

### 12.1 源码复现

1. 使用 Keil/ARM 工具链打开工程文件；当前审计已确认有 `OBJ/PWM.axf` 和 `USER/PWM.map`，但尚未确认工程项目文件的确切名称。
2. 核对 target 的 CPU、宏定义、include 路径、启动文件和链接地址。
3. 确认 `ifopen`、`DEBUG` 等编译宏，并确认 IAP 是否进入目标固件。
4. 编译并检查 map 文件中的 Flash/RAM 使用、向量表位置、IAP/APP 分区和栈大小。
5. 生成烧录文件后，先在无动力/限流条件下验证启动、按键、LCD、传感器和编码器，再验证电机 PWM。

不应直接把现存 `OBJ/PWM.axf` 当作本次重建产物：它只是目录中已存在的文件，尚未核对时间、配置和源码是否一一对应。

### 12.2 最小硬件验证顺序

| 阶段 | 验证内容 | 通过证据 |
|---|---|---|
| 上电 | 复位、时钟、LCD 初始化 | LCD 出现 `Init Complete! `，无异常复位 |
| 输入 | PB1/PB12 按键事件 | 模式/档位变化与蜂鸣行为符合源码 |
| 传感器 | DHT/MQ2 采集和算法 | 状态值稳定更新，边界值触发规则正确 |
| 反馈 | 编码器、TIM2 溢出、TIM4 唤醒 | 速度读数方向和量纲正确 |
| 手动控制 | LOW/HIGH 目标 RPM + PID | PWM 不越界，转速闭环稳定 |
| 自动控制 | Startup/Cooking/DelayOff | 事件、10 秒延时关闭、再次触发均正确 |
| 防回流 | NORMAL/HIGH 双阈值 | 启停滞回有效，极端浓度保护有效 |
| 升级 | DMA 收包、CRC、APP 跳转 | 成功、失败、掉电和非法包路径均有证据 |

### 12.3 算法单元测试建议

在不接电机的条件下，可直接对 `WindSpeed_Update()` 做以下确定性测试：

- `T<=20`、`H<=40`、`G<=80`：三路归一化应被限制为 0，PWM 应为 20%。
- `T>=35`、`H>=75`、`G>=450`：三路归一化应被限制为 1，融合值应为 1，PWM 应为 100%。
- `T=26`、`H=50`、`G=100`：由于代码使用严格 `>`，不应判为 Cooking Event。
- 温度、湿度、气体分别单独升高：气体变化对融合值的影响最大，权重为 0.6。
- `WindSpeed_GetTargetRPM(0)` 返回 190，`(1)` 返回 220，其他值回退到 190。

这些是由源码公式直接推出的单元测试预期，不等同于已经执行过的测试结果。

## 13. 验证记录与未验证项

### 13.1 本次已完成

- 已从源码定位入口、硬件初始化、任务创建、模式切换、速度反馈、自动控制、防回流和 IAP 关键函数。
- 已核对 `SystemState_t`、任务优先级/栈大小、算法常量、PID 参数和 PWM/编码器接口。
- 已确认工程目录存在 `OBJ/PWM.axf`、`USER/PWM.map` 两个构建产物文件。
- 已对新版和 `APPV1.1` 旧版入口行为做源码级区分。
- 已将地址/容量矛盾、`ifopen` 条件编译和运行时并发点明确标出。

### 13.2 本次未完成或不能从当前证据确认

- 未执行 Keil/ARM 编译，因此不能保证当前源码在现有环境中可重建。
- 未烧录或连接 STM32 板，未验证 LCD、按键、传感器、PWM、电机和编码器物理行为。
- 未执行 `PWM.axf` 的反汇编、符号表或 map 解析，不能用其证明当前源码的最终内存布局。
- 未确认完整 `.uvprojx/.uvproj`、启动文件、scatter/linker 配置和发布脚本。
- 未确认 DHT11、MQ2、编码器的真实引脚、标定曲线、单位和电气接口。
- 未确认 `GAS_THRESHOLD_NORMAL/HIGH` 的具体数值和传感器校准方法。
- 未完成 IAP 协议、CRC32、Flash 擦写和断电恢复的端到端测试。
- 未完成任务栈余量、堆余量、优先级反转、共享变量原子性和 `delay_ms()` 调度语义验证。

## 14. 风险与接手检查清单

1. **升级地址风险**：注释和版本入口对 IAP/APP 地址与容量的描述不完全一致。先看链接文件和 bootloader，再生成升级包。
2. **共享状态风险**：`g_systemState`、`speed`、`overflow` 同时被任务和 ISR/驱动访问；应审查锁、临界区和原子读取。
3. **周期语义风险**：多个任务使用 `delay_ms()`，必须确认它是否阻塞并让出 FreeRTOS CPU。
4. **电机安全风险**：验证前应断开负载或使用限流电源；确认刹车输入 PB12 与 H 桥使能 PA2 的安全电平。
5. **阈值标定风险**：MQ2 原始值到 `gasConcentration` 的转换未在本文假设为已标定，NORMAL/HIGH 和算法基线需实测。
6. **控制量单位风险**：风速算法输出为百分比，自动路径再转为 `MAXCCR` 比较值；手动 PID 输出为 0..1000。必须确认每个 `motor_pwm_set` 封装的输入单位一致。
7. **初始化状态风险**：`motor_init()` 会调用 `motor_start()`，启动阶段的实际通道输出和电机安全状态需要示波器/万用表及断开负载确认。
8. **版本一致性风险**：AXF/MAP 可能来自不同源码或不同宏配置；构建时间、Git/归档版本和 map 入口应在发布前绑定。

接手前至少应把以下证据补齐：

```text
[ ] 工程项目文件和工具链版本
[ ] 启动文件、链接文件、VTOR 与 APP 起始地址
[ ] bootloader/IAP 协议、CRC 规则和 Flash 分区图
[ ] 每个传感器及编码器的引脚、单位、标定参数
[ ] motor_pwm_set 的实现、PWM 极性和刹车逻辑
[ ] delay_ms 的 RTOS 语义
[ ] 任务栈/堆运行时统计
[ ] 实机启动、按键、传感、闭环、自动、防回流和升级记录
```

## 15. 关键入口索引

| 需要回忆的功能 | 首先打开 |
|---|---|
| 启动顺序、VTOR、外设初始化 | `USER/main.c` |
| 全局对象和头文件依赖 | `USER/main.h`、`APP_TASK/app_tasks.h` |
| 状态初始化、任务创建 | `APP_TASK/app_tasks.c` 中 `System_Init()`、`StartTask_Create()` |
| 按键事件 | `APP_TASK/app_tasks.c` 中 `KeyScanTask()` |
| 传感器和融合更新 | `APP_TASK/app_tasks.c` 中 `SensorTask()`、`WindSpeedTask()`；`BSP/WIND/wind_speed.c` |
| 模式控制 | `APP_TASK/app_tasks.c` 中 `MotorControlTask()`、`System_SwitchMode()` |
| 防回流 | `APP_TASK/app_tasks.c` 中 `AntiBackflowTask()` |
| 手动启停 | `APP_TASK/app_tasks.c` 中 `System_ToggleMotor()` |
| 速度反馈 | `SpeedCalcTask()`、`TIM2_IRQHandler()`、`TIM4_IRQHandler()` |
| PWM/H 桥 | `BSP/MOTOR/motor.c` |
| IAP | `APP_TASK/app_tasks.c` 中 `iap_task()` 及 DMA ISR；`USER/main.c` 中 DMA 配置 |
| 任务/常量速查 | `APP_TASK/app_tasks.h` |

## 16. 结论

当前源码已经形成一个可理解的 FreeRTOS 油烟机控制框架：传感器融合负责自动风量建议，编码器测速和 PID 负责手动档位闭环，模式任务负责策略切换，防回流任务负责气体阈值滞回，DMA/IAP 负责可选升级通道。接手开发的第一优先级不是重写控制逻辑，而是把工程文件、链接布局、IAP 分区、传感器标定、PWM 单位和实机证据补齐。特别是 `0x0800F000` 可以作为当前源码显示出的 APP 地址意图，但在 bootloader、链接文件和容量矛盾消除前，不应把它当作已批准的量产升级布局。
