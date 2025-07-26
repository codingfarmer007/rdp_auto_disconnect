REM NetworkProfile/4004

start /b "rdp_auto_disconnect" "%~dp0\rdp_auto_disconnect.exe"
"%~dp0\frpc.exe" -c "%~dp0\frpc.toml" > "%~dp0\frp.log"
shutdown /r /f /t 30