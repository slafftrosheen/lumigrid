@echo off
cd /d "%~dp0LumigridLedNode"
echo ===================================================================
echo  LumiGrid - Flashing Production Firmware to ESP32 (COM3)
echo ===================================================================
echo.
echo  If esptool waits for connection:
echo    1. Press and hold [BOOT] (or IO0).
echo    2. Tap [EN] (or RST) once.
echo    3. Release [BOOT] when writing starts.
echo.
"C:\Users\slavd\.platformio\penv\Scripts\python.exe" "C:\Users\slavd\.platformio\packages\tool-esptoolpy\esptool.py" --port COM3 --baud 115200 --before default_reset --after hard_reset --connect-attempts 40 write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x1000 .pio\build\factory\bootloader.bin 0x8000 .pio\build\factory\partitions.bin 0xd000 .pio\build\factory\ota_data_initial.bin 0x10000 .pio\build\factory\firmware.bin
echo.
if %ERRORLEVEL% EQU 0 (
    echo ===================================================================
    echo  SUCCESS! Flashed and rebooted.
    echo ===================================================================
) else (
    echo ===================================================================
    echo  FAILED to connect. Tip: Hold BOOT, tap EN/RST, then run again.
    echo ===================================================================
)
pause
