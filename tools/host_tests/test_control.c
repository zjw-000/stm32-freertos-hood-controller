/* Production functions are included by run_control_selftest.py; only hardware and
 * scheduling are mocked. Lock assertions catch I/O inside the shared-state lock. */
#include <assert.h>
#include <float.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "app_tasks.h"
#include "wind_speed.h"
#include "pid.h"
#include "mq2.h"
#include "auto_state.inc"

#define GPIOC 1
#define GPIO_Pin_14 14
#define BLUE 1
#define WHITE 0
#define ADC1 1
#define ADC_FLAG_EOC 1
#define ADC_SampleTime_239Cycles5 1
#define ADC_Channel_4 4
#define ENABLE 1
#define DISABLE 0
#define MCU_APP_ADDR 0x08002000U
#define SENSOR_DEBUG 0

static SystemState_t g_systemState;
static AutoModeState_t g_autoModeState;
static u8 s_pidReset, s_appReady, g_iapUpgradeActive;
static TickType_t s_windDhtTick, s_windGasTick, s_progressTick, s_operationStart;
static uint32_t s_healthyRounds;
static PID_TypeDef g_speedPID;
static SemaphoreHandle_t g_dataMutex = (void*)1, g_speedCalcSemaphore = (void*)2;
static float speed;
static const char *ModeNames[] = {"Standby", "Manual ", "Auto   ", "Anti-BF"};
static const char *SpeedLevelNames[] = {"LOW ", "HIGH"};
static const char *AutoStateNames[] = {"Startup ", "Cooking ", "DelayOff"};
static struct { struct { unsigned size, crc32; } active; } s_startMeta;

static TickType_t now;
static int locked, motor_on, motor_calls, dht, dht_ok, adc_ok, adc_reads;
static int heartbeats;
static int task_loop, selftest_loop, enter_ota_on_take, ota_after_motor_unlock;
static int mutate_ui, ui_calls, flash_calls;
static u8 sample_temp, sample_humi;
static u16 adc_sample;
static float pwm_output;
static jmp_buf loop_exit;
static int IapPlatform_Enter(void);
static void (*sample_hook)(void);

static TickType_t xTaskGetTickCount(void) { return now; }
static int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{
    (void)timeout;
    if (mutex == g_dataMutex) {
        assert(!locked);
        if (enter_ota_on_take) { enter_ota_on_take = 0; g_iapUpgradeActive = 1; }
        locked = 1;
    }
    return pdTRUE;
}
static void xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex == g_dataMutex && locked);
    locked = 0;
    if (ota_after_motor_unlock) {
        ota_after_motor_unlock = 0;
        assert(IapPlatform_Enter());
    }
}
static void Heartbeat(unsigned bit) { (void)bit; assert(!locked); if (task_loop && heartbeats++) longjmp(loop_exit, 1); }
static void vTaskDelay(unsigned ticks)
{
    assert(!locked);
    if (selftest_loop) {
        now += ticks;
        if (sample_hook) sample_hook();
        return;
    }
    assert(task_loop);
    longjmp(loop_exit, 1);
}
static void delay_ms(unsigned ms) { vTaskDelay(ms); }
static void delay_us(unsigned us) { (void)us; assert(!locked); }
static int DHT_Read_Data(u8 *t, u8 *h, int port, int pin, int *io)
{
    (void)port; (void)pin; (void)io;
    assert(!locked);
    if (dht_ok) { *t = sample_temp; *h = sample_humi; }
    return dht_ok;
}
static void ADC_RegularChannelConfig(int adc, int channel, int rank, int cycles)
{ (void)adc; (void)channel; (void)rank; (void)cycles; assert(!locked); }
static void ADC_ClearFlag(int adc, int flag) { (void)adc; (void)flag; }
static void ADC_SoftwareStartConvCmd(int adc, int state) { (void)adc; (void)state; }
static int ADC_GetFlagStatus(int adc, int flag) { (void)adc; (void)flag; return adc_ok; }
static u16 ADC_GetConversionValue(int adc) { (void)adc; adc_reads++; return adc_sample; }
static void motor_stop(void) { assert(locked); motor_on = 0; motor_calls++; }
static void motor_start(void) { assert(locked); assert(!g_iapUpgradeActive); motor_on = 1; motor_calls++; }
static void motor_pwm_set(float value)
{ assert(locked); assert(!g_iapUpgradeActive); pwm_output = value; motor_calls++; }
static int get_encoder_value(void) { assert(!locked); return 123; }
static float get_speed(int count, int ms) { assert(count == 123 && ms == 50); return 198.0f; }
static int W25QXX_Ready(void) { assert(!locked); return 1; }
static int BootStore_Read(void *meta)
{ (void)meta; assert(!locked); flash_calls++; s_startMeta.active.size = 100; return 1; }
static int BootImage_SizeValid(const void *image) { (void)image; return 1; }
static unsigned BootCrc32(const uint8_t *data, unsigned size)
{ (void)data; (void)size; assert(!locked); return s_startMeta.active.crc32; }
static void LCD_Clear(int color) { (void)color; assert(!locked); }
static void Show_Str(int x, int y, int fg, int bg, u8 *text, int size, int mode)
{
    (void)x; (void)fg; (void)bg; (void)size; (void)mode;
    assert(!locked);
    ui_calls++;
    if (mutate_ui) {
        if (y == 0) { System_SwitchMode(); System_SwitchSpeedLevel(); }
        if (y == 20) assert(strcmp((char*)text, "Mode:Manual ") == 0);
        if (y == 40) assert(strcmp((char*)text, "Level:LOW ") == 0);
    }
}
#include "control_under_test.inc"

static void once(void (*task)(void*))
{
    task_loop = 1; heartbeats = 0;
    if (setjmp(loop_exit) == 0) task(NULL);
    task_loop = 0;
    assert(!locked);
}
static void reset(void)
{
    memset(&g_systemState, 0, sizeof(g_systemState));
    g_autoModeState = AUTO_STATE_STARTUP;
    s_pidReset = 1; s_appReady = 1; g_iapUpgradeActive = 0;
    now = 100; s_windDhtTick = s_windGasTick = 0;
    locked = motor_on = motor_calls = adc_reads = 0;
    enter_ota_on_take = ota_after_motor_unlock = mutate_ui = ui_calls = 0;
    flash_calls = 0; sample_hook = NULL;
    s_healthyRounds = 16;
    dht_ok = adc_ok = 1; adc_sample = 1500;
    sample_temp = 30; sample_humi = 60;
    pwm_output = 0;
    PID_Init(&g_speedPID, 1.0f, 0.1f, 0.0f, 1000.0f, 0.0f);
    WindSpeed_Init();
}
static void set_mode(WorkMode_t mode)
{
    xSemaphoreTake(g_dataMutex, portMAX_DELAY);
    State_SetModeLocked(mode);
    xSemaphoreGive(g_dataMutex);
}
static void gas(float concentration)
{
    g_systemState.gasConcentration = concentration;
    g_systemState.gasValid = 1;
    g_systemState.gasSampleTick = now;
}
static void closef(float actual, float expected)
{ assert(fabsf(actual - expected) < 0.0002f); }
static void test_wind(void)
{
    int t, h;
    float previous;
    reset();
    WindSpeed_Update(0, 0, 0);
    closef(WindSpeed_GetPWM(), 20);
    WindSpeed_Update(255, 255, 10000);
    closef(WindSpeed_GetPWM(), 100);
    previous = 19.0f;
    for (t = TEMP_BASE; t <= TEMP_MAX; t++) {
        WindSpeed_Update((u8)t, HUMIDITY_BASE, GAS_BASE);
        closef(WindSpeed_GetData()->f_T, (float)(t-TEMP_BASE)/(TEMP_MAX-TEMP_BASE));
        assert(WindSpeed_GetPWM() > previous);
        previous = WindSpeed_GetPWM();
    }
    previous = 19.0f;
    for (h = HUMIDITY_BASE; h <= HUMIDITY_MAX; h++) {
        WindSpeed_Update(TEMP_BASE, (u8)h, GAS_BASE);
        closef(WindSpeed_GetData()->f_H, (float)(h-HUMIDITY_BASE)/(HUMIDITY_MAX-HUMIDITY_BASE));
        assert(WindSpeed_GetPWM() > previous);
        previous = WindSpeed_GetPWM();
    }
    WindSpeed_Update(26, 54, 228.0f); /* All three normalized inputs = 0.4. */
    closef(WindSpeed_GetPWM(), 52.0f);
    assert(WindSpeed_GetPWMCompare(1000) == 520);
    puts("PASS wind: fractional normalization, monotonic steps, clamping, weights and PWM");
}
static void test_sensors(void)
{
    float old_gas, value = 123;
    int reads;
    reset();
    once(SensorTask); once(WindSpeedTask);
    assert(g_systemState.dhtValid && g_systemState.gasValid && g_systemState.windValid);
    assert(g_systemState.cookingEventActive);
    old_gas = g_systemState.gasConcentration; reads = adc_reads;
    dht_ok = 0; adc_sample = 2500; now += 500;
    once(SensorTask);
    assert(!g_systemState.dhtValid && g_systemState.gasValid);
    assert(adc_reads == reads + 10 && g_systemState.gasConcentration > old_gas);
    assert(!g_systemState.windValid && !g_systemState.cookingEventActive);
    set_mode(MODE_AUTO); once(MotorControlTask); assert(!motor_on);
    set_mode(MODE_ANTI_BACKFLOW); once(AntiBackflowTask); once(MotorControlTask);
    assert(motor_on); /* DHT failure must not disable valid gas control. */
    dht_ok = 1; adc_ok = 0; sample_temp = 31; now += 500;
    once(SensorTask);
    assert(g_systemState.dhtValid && g_systemState.temperature == 31);
    assert(!g_systemState.gasValid && !g_systemState.antiBackflowActive);
    once(MotorControlTask); assert(!motor_on);
    assert(MQ2_GetAdcValue() == 0xFFFFU);
    assert(MQ2_GetGasConcentration() == -1.0f && !MQ2_IsGasDetected(100));
    adc_ok = 1; adc_sample = 0;
    assert(!MQ2_ReadGasConcentration(&value) && value == 123);
    adc_sample = 4095; assert(MQ2_ReadGasConcentration(&value) && value > 100);
    assert(!MQ2_ReadGasConcentration(NULL));
    once(SensorTask); once(WindSpeedTask);
    set_mode(MODE_AUTO); once(MotorControlTask); assert(motor_on);
    now += 500; once(SensorTask); once(MotorControlTask);
    assert(motor_on); /* A new successful sample must not cause periodic stop pulses. */
    puts("PASS sensors: independent failure/recovery, ADC timeout/zero/high, no sample stop pulse");
}
static void test_freshness(void)
{
    SystemState_t snapshot;
    reset(); once(SensorTask); once(WindSpeedTask); set_mode(MODE_AUTO);
    now += SENSOR_MAX_AGE_MS;
    once(MotorControlTask); assert(motor_on);
    now++;
    once(MotorControlTask); assert(!motor_on);
    System_GetState(&snapshot);
    assert(!snapshot.dhtValid && !snapshot.gasValid && !snapshot.windValid);
    assert(snapshot.cookingEventActive == 0 && snapshot.windSpeedPWM == 0);
    /* Samples are fresh but the calculated PWM is older than its validity window. */
    once(SensorTask); once(MotorControlTask); assert(!motor_on);
    once(WindSpeedTask); once(MotorControlTask); assert(motor_on);
    /* Tick rollover uses unsigned subtraction. */
    reset(); now = UINT32_MAX - 100; once(SensorTask); once(WindSpeedTask);
    now += 1000; System_GetState(&snapshot); assert(snapshot.dhtValid && snapshot.gasValid);
    now += 501; System_GetState(&snapshot); assert(!snapshot.dhtValid && !snapshot.gasValid);
    puts("PASS freshness: 1500/1501 ms boundary, stale fusion, recovery and tick wrap");
}
static void test_backflow(void)
{
    const float levels[] = {99, 100, 75, 50, 49, 50, 99, 2000, 10000, 49};
    const int active[] = {0, 1, 1, 1, 0, 0, 0, 1, 1, 0};
    unsigned i;
    reset(); set_mode(MODE_ANTI_BACKFLOW);
    for (i = 0; i < sizeof(levels)/sizeof(levels[0]); i++) {
        int before = motor_calls;
        gas(levels[i]); once(AntiBackflowTask);
        assert(motor_calls == before); /* Policy task cannot write the motor. */
        assert(g_systemState.antiBackflowActive == active[i]);
        closef(g_systemState.gasThreshold, active[i] ? 50 : 100);
        once(MotorControlTask); assert(motor_on == active[i]);
    }
    gas(100); once(AntiBackflowTask); once(MotorControlTask); assert(motor_on);
    now += SENSOR_MAX_AGE_MS + 1;
    once(MotorControlTask); assert(!motor_on && !g_systemState.antiBackflowActive);
    gas(75); once(AntiBackflowTask); assert(!g_systemState.antiBackflowActive);
    gas(100); once(AntiBackflowTask); assert(g_systemState.antiBackflowActive);
    System_SwitchMode(); assert(g_systemState.currentMode == MODE_STANDBY);
    System_SwitchMode(); System_SwitchMode(); System_SwitchMode();
    assert(g_systemState.currentMode == MODE_ANTI_BACKFLOW && !g_systemState.antiBackflowActive);
    gas(75); once(AntiBackflowTask); once(MotorControlTask); assert(!motor_on);
    puts("PASS backflow: hysteresis 49/50/99/100/2000/10000, stale gas, rapid exit/re-entry");
}
static void test_control(void)
{
    int before;
    SystemState_t snapshot;
    reset();
    once(MotorControlTask); assert(!motor_on);
    System_SwitchMode(); assert(g_systemState.currentMode == MODE_MANUAL);
    assert(motor_calls == 1); /* Key request caused no physical write. */
    once(SpeedCalcTask); once(MotorControlTask);
    assert(motor_on && g_systemState.targetRPM == SPEED_LOW_RPM);
    closef(g_systemState.actualRPM, 198);
    System_SwitchSpeedLevel(); once(MotorControlTask);
    assert(g_systemState.targetRPM == SPEED_HIGH_RPM);
    before = motor_calls;
    System_ToggleMotor(); assert(motor_calls == before);
    once(MotorControlTask); assert(!motor_on && g_speedPID.integral == 0);
    System_ToggleMotor(); once(MotorControlTask); assert(motor_on);
    System_GetState(&snapshot); snapshot.currentMode = MODE_STANDBY;
    assert(g_systemState.currentMode == MODE_MANUAL);
    /* Simulate OTA starting while a key caller was waiting for the state lock. */
    enter_ota_on_take = 1; System_SwitchMode();
    assert(g_systemState.currentMode == MODE_MANUAL);
    once(MotorControlTask); assert(!motor_on);
    g_iapUpgradeActive = 0; s_appReady = 0; once(MotorControlTask); assert(!motor_on);
    s_appReady = 1;
    ota_after_motor_unlock = 1; once(MotorControlTask);
    assert(g_iapUpgradeActive && !motor_on && !g_systemState.motorRunning);
    assert(g_systemState.currentMode == MODE_STANDBY);
    System_ToggleMotor(); System_SwitchSpeedLevel(); once(MotorControlTask); assert(!motor_on);
    /* A mode change during LCD transfers cannot mix old/new fields on screen. */
    g_iapUpgradeActive = 0; set_mode(MODE_MANUAL); g_systemState.speedLevel = SPEED_LOW;
    mutate_ui = 1; once(UIDisplayTask);
    assert(ui_calls == 8 && g_systemState.currentMode == MODE_AUTO);
    puts("PASS ownership: four modes/PID, locked snapshots, UI unlocked, queued key/OTA interleavings");
}
static void test_auto_timers(void)
{
    reset(); once(SensorTask); once(WindSpeedTask); set_mode(MODE_AUTO);
    once(MotorControlTask); assert(g_autoModeState == AUTO_STATE_COOKING && motor_on);
    g_systemState.cookingEventActive = 0;
    once(MotorControlTask); assert(g_autoModeState == AUTO_STATE_DELAY_OFF);
    g_systemState.cookingEventCounter = COOKING_EVENT_DELAY_OFF - 50;
    once(MotorControlTask); assert(!motor_on && g_systemState.currentMode == MODE_STANDBY);
    set_mode(MODE_AUTO); g_systemState.autoModeCounter = AUTO_MODE_STARTUP_TIME - 50;
    once(MotorControlTask); assert(!motor_on && g_systemState.currentMode == MODE_STANDBY);
    set_mode(MODE_AUTO); g_autoModeState = AUTO_STATE_COOKING;
    g_systemState.cookingEventActive = 1;
    g_systemState.cookingEventCounter = COOKING_EVENT_TIMEOUT - 50;
    once(MotorControlTask); assert(!motor_on && g_systemState.currentMode == MODE_STANDBY);
    puts("PASS automatic mode: startup/cooking/delay-off transitions and existing timeouts");
}
static void publish_healthy(void)
{ g_systemState.dhtSampleTick = g_systemState.gasSampleTick = now; }
static void test_health(void)
{
    reset(); once(SensorTask); sample_hook = publish_healthy; selftest_loop = 1;
    assert(App_SelfTest()); assert(flash_calls == 1);
    g_systemState.dhtValid = 0; assert(!App_SelfTest());
    g_systemState.dhtValid = 1; g_systemState.gasValid = 0; assert(App_SelfTest());
    g_systemState.gasValid = 1; s_healthyRounds = 15; assert(!App_SelfTest());
    s_healthyRounds = 16; sample_hook = NULL; assert(!App_SelfTest());
    assert(flash_calls == 2); selftest_loop = 0;
    puts("PASS health gate: existing DHT/watchdog/Flash/image checks, MQ2 failure stays isolated");
}
int main(void)
{
    test_wind(); test_sensors(); test_freshness(); test_backflow();
    test_control(); test_auto_timers(); test_health();
    puts("All control regression tests passed.");
    return 0;
}
