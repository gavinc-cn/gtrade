@echo off
REM GTrade - Protobuf Code Generator
REM This script generates C++ code from .proto files

REM ====== Configuration ======
set PROTOC=../../../tools\protoc-21.12-win64\bin\protoc.exe
set PROTO_DIR=../../../proto
set OUTPUT_DIR=../../../src\proto
set GRPC_PLUGIN=../../../vcpkg_installed/x64-windows/tools/grpc/grpc_cpp_plugin.exe

for %%f in (%PROTO_DIR%\*.proto) do (
    echo Processing: %%~nxf
    "%PROTOC%" --cpp_out=%OUTPUT_DIR% --grpc_out=%OUTPUT_DIR% --plugin=protoc-gen-grpc="%GRPC_PLUGIN%" -I=%PROTO_DIR% "%%f"
)
