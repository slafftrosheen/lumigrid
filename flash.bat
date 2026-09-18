@echo off
cd /d "%~dp0LumigridLedNode"
echo ===================================================================
echo  LumiGrid - Flashing Production Firmware to ESP32 (COM7)
echo ===================================================================
echo.
echo  If connection is waiting:
echo    1. Press and hold [BOOT] (or IO0).
echo    2. Tap [EN] (or RST) once.
echo    3. Release [BOOT] when writing starts.
echo.
"C:\Python314\python.exe" "C:\Users\alisi\.platformio\packages\tool-esptoolpy\esptool.py" --port COM7 --baud 460800 --before no_reset --connect-attempts 60 write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x1000 .pio\build\factory\bootloader.bin 0x8000 .pio\build\factory\partitions.bin 0xd000 .pio\build\factory\ota_data_initial.bin 0x10000 .pio\build\factory\firmware.bin
echo.
if %ERRORLEVEL% EQU 0 (
    echo ===================================================================
    echo  SUCCESS! Flashed and rebooted.
    echo ===================================================================
) else (
    echo ===================================================================
    echo  FAILED to connect. Tip: Unplug USB, hold BOOT, plug USB back in.
    echo ===================================================================
)
pause
