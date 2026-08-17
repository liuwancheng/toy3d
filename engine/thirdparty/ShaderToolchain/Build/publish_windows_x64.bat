@echo off
setlocal
python "%~dp0publish_shader_toolchain.py" %*
exit /b %errorlevel%
