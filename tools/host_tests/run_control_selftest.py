"""Execute exact production task/driver functions with mocked RTOS and peripherals.
Requires Python 3 and GCC. No hardware is accessed; build files go to ignored work/.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def function(source, name):
    match = re.search(r"^(?:static )?(?:void|int|float|u8|u16|uint8_t)\s+" +
                      re.escape(name) + r"\([^;{}]*\)\s*\{", source, re.M)
    if not match:
        raise ValueError(f"Missing production function: {name}")
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "work")
    args = parser.parse_args()
    args.build_dir.mkdir(parents=True, exist_ok=True)
    gcc = shutil.which("gcc")
    if not gcc:
        raise SystemExit("GCC not found on PATH")
    app = (ROOT / "APP_TASK/app_tasks.c").read_text(encoding="utf-8-sig")
    mq = (ROOT / "BSP/MQ2/mq2.c").read_text(encoding="utf-8-sig")
    functions = ["State_ExpireSensorsLocked", "State_SetModeLocked", "SensorTask",
                 "WindSpeedTask", "MotorControlTask", "UIDisplayTask", "AntiBackflowTask",
                 "System_SwitchMode", "System_SwitchSpeedLevel", "System_ToggleMotor",
                 "System_GetState", "SpeedCalcTask", "App_SelfTest", "IapPlatform_Enter"]
    # Guard ownership, including unextracted functions. GPIO writes are allowed only
    # in the normal owner or the two immediate safety-stop paths.
    for match in re.finditer(r"^(?:static )?(?:void|int|uint8_t)\s+(\w+)\([^;{}]*\)\s*\{", app, re.M):
        name = match.group(1)
        body = function(app, name)
        if re.search(r"\bmotor_(?:start|stop|pwm_set)\s*\(", body):
            assert name in {"MotorControlTask", "App_Fatal", "IapPlatform_Enter"}, name
    assert "return &g_systemState" not in app
    assert "GAS_THRESHOLD_HIGH" not in app
    with tempfile.TemporaryDirectory(prefix="control-", dir=args.build_dir) as temp:
        build = Path(temp)
        # Only dependencies/types are stubbed. Functions under test are copied verbatim
        # from the current source; no parallel implementation of the control logic.
        (build / "sys.h").write_text("#pragma once\n#include <stdint.h>\n"
            "typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;\n")
        (build / "FreeRTOS.h").write_text("#pragma once\n#include <stdint.h>\n"
            "typedef uint32_t TickType_t; typedef int BaseType_t;\n"
            "typedef void *SemaphoreHandle_t;\n#define pdTRUE 1\n#define portMAX_DELAY UINT32_MAX\n"
            "#define pdMS_TO_TICKS(n) ((TickType_t)(n))\n")
        for header in ["task.h", "queue.h", "semphr.h", "dma.h", "stmflash.h", "iap.h"]:
            (build / header).write_text("#pragma once\n")
        enum = re.search(r"typedef enum \{\s*AUTO_STATE_STARTUP.*?\} AutoModeState_t;", app, re.S).group()
        (build / "auto_state.inc").write_text(enum, encoding="utf-8")
        bodies = [function(mq, n) for n in ["MQ2_GetAdcValue", "MQ2_ReadGasConcentration",
                  "MQ2_GetGasConcentration", "MQ2_IsGasDetected"]]
        bodies += [function(app, n) for n in functions]
        (build / "control_under_test.inc").write_text("\n\n".join(bodies), encoding="utf-8")
        executable = build / "test_control.exe"
        include = [build, ROOT/"APP_TASK", ROOT/"BSP/WIND", ROOT/"BSP/PID", ROOT/"BSP/MQ2"]
        subprocess.run([gcc, "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror",
            *["-I"+str(path) for path in include], str(HERE/"test_control.c"),
            str(ROOT/"BSP/WIND/wind_speed.c"), str(ROOT/"BSP/PID/pid.c"), "-lm", "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    print("Control ownership checks: PASS")


if __name__ == "__main__":
    main()
