# CTPAPI 第三方依赖

本目录存放上期技术 CTPAPI（综合交易平台客户端接口）的**官方包**，用于接入
中国期货市场（CTP 柜台）。采用**标准版(se)**，版本 **v6.7.13**（2026-02-25）。

## 目录结构（官方包，头文件与 .so 同包，无需单独拷贝）

```
src/3rd/ctp/
├── v6.7.13_20260225_api_mduserapi_se_linux64/      行情
│   ├── ThostFtdcMdApi.h            行情接口 (MdApi/MdSpi)
│   ├── ThostFtdcUserApiDataType.h  类型/枚举（与交易包同源）
│   ├── ThostFtdcUserApiStruct.h    数据结构 *Field（与交易包同源）
│   └── thostmduserapi_se.so        行情库（标准版）
└── v6.7.13_20260225_api_traderapi_se_linux64/     交易
    ├── ThostFtdcTraderApi.h        交易接口 (TraderApi/TraderSpi)
    ├── ThostFtdcUserApiDataType.h
    ├── ThostFtdcUserApiStruct.h
    └── thosttraderapi_se.so         交易库（标准版）
```

> CTP API 不走包管理器（无 apt/conan），且头文件是和 .so **强 ABI 耦合**的契约
> （结构体布局决定 ABI），必须同版本配对，故整套包直接放项目内。

## CMake 集成（src/CMakeLists.txt）

- CTP **默认随 gtrade 编译**（无开关，与 OKX 并列的一等接入）。
- 通配符自动定位包目录（`*mduserapi*se*linux64` / `*traderapi*se*linux64`），
  **升级版本无需改 CMake**（换版本号目录名即可）。
- 链接库名：`thostmduserapi_se` / `thosttraderapi_se`（标准版带 `_se` 后缀），
  只链接给 gtrade（回测 gtrade_bt 不含 CTP 源码、不依赖 CTP .so）。
- GBK 头以 `include_directories(SYSTEM ...)` 处理，避免编码告警。

## 连接目标（标准版 se 库连官方 CTP 柜台）

| 环境 | 交易前置 | 行情前置 | 认证 |
|------|---------|---------|------|
| **SimNow 官方模拟**（联调） | `tcp://180.168.146.187:10201` 等（以 SimNow 公告为准） | 同区行情前置 | 投资者账号+密码 |
| **实盘 CTP 柜台** | 券商提供 | 券商提供 | 需 BrokerID + AppID + AuthCode |

> 想用 **openctp 7x24**（免注册）联调时，需把库换成 tts 版（`thostmduserapi.so`，
> 无 `_se`），头文件相同（"换库不换码"）。届时 CMake 库名相应调整。

## ABI / 编码注意

- 结构体大小由 Struct.h(6.7.13) 决定，必须与本目录 `.so`(6.7.13) 匹配。
- 头文件为 GBK 编码（上期技术原厂），注释含非 UTF-8 字节；以 SYSTEM 头处理，
  不影响构建，源码中勿复制其注释。
