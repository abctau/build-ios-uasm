# AGENTS.md

面向智能编码代理的仓库指南。本项目是 **uni-app x（vapor 模式）宿主应用 + UASM 原生插件 `test-uasm`** 的测试工程：C++ 原生代码经 uni-gyp 编译为各端动态库/框架，Web 与小程序端经 Emscripten 编译为 WASM，统一打包进 `uni_modules/test-uasm`，由前端页面通过 `uni.loadUasm` 加载验证。

## 项目结构与模块组织

```
├── main.uts / App.uvue          # uni-app x 应用入口
├── manifest.json                # uni-app x 应用清单（vapor: true；Android abiFilters: arm64-v8a, x86_64）
├── pages.json                   # 页面路由配置（仅 pages/index/index）
├── index.d.ts                   # 插件 TS 声明（与 uni_modules 内副本保持一致）
├── pages/index/index.uvue       # 唯一测试页面：加载 uasm 模块、校验 add(1,2)、压测
├── src/                         # ★ 原生插件 C++ 源码
│   ├── binding.cc               # N-API 绑定（Android/iOS/Harmony 共用，C++20，禁用异常）
│   ├── binding.gyp              # uni-gyp 构建配置（目标名 UasmTestUasm）
│   └── wasm_binding.cc          # Emscripten 绑定（embind，仅 Web/小程序）
├── scripts/
│   ├── build-wasm.js            # 不依赖 make 的 WASM 构建脚本（需 EMSDK）
│   └── pack-wasm.js             # 将 WASM 产物分发打包到 uni_modules
├── uni_modules/test-uasm/uasm/  # ★ 预构建产物（勿手改，由构建命令生成）
│   ├── index.d.ts               # 插件类型声明（NativePlugin: add）
│   ├── app-android/libs/        # arm64-v8a、x86_64 的 .so
│   ├── app-harmony/libs/        # arm64-v8a、x86_64 的 .so
│   ├── app-ios/frameworks/      # UasmTestUasm.xcframework
│   ├── web/                     # test-uasm.js + .wasm
│   └── mp-weixin/ mp-alipay/    # test-uasm.js + brotli 压缩的 .wasm.br
├── web/release/                 # WASM 构建中间产物
├── static/                      # 静态资源（logo 等）
├── Makefile                     # emscripten 官方构建流程（emmake make）
└── .github/workflows/build-ios.yml  # 手动触发的 iOS 构建流水线（workflow_dispatch）
```

- 无自动化测试目录/框架；验证方式是运行 App 后在测试页点击按钮，或使用 HBuilderX 的语法校验。
- `unpackage/`、`node_modules/`、`build/` 均为生成物，不要提交或编辑。

## 构建 / 打包命令

原生各端构建（产物落到 `uni_modules/test-uasm/uasm/`，依赖本机 SDK 路径，package.json 中硬编码）：

- `npm run build:test-uasm:android:release` — Android（需 E:/hbAndroidSdk + NDK）
- `npm run build:test-uasm:harmony:release` — 鸿蒙（需 DevEco NDK + cmake）
- `npm run build:test-uasm:ios:release` — iOS xcframework（arm64, x64）
- `npm run build:test-uasm:pack:uni-module:release` — `uni-gyp module-pack` 打包 uni_modules
- `npm run build:test-uasm:all` — 依次执行上述四步

WASM（需 emsdk，默认 `E:/emsdk`，可用环境变量 `EMSDK` 覆盖）：

- `npm run build:test-uasm:wasm:release` — 编译 `src/wasm_binding.cc` → `web/release/`
- `npm run pack:test-uasm:wasm:release` — 分发到 `uasm/{web,mp-weixin,mp-alipay}`（小程序端自动 brotli 压缩）
- `npm run build:test-uasm:all:wasm` — 构建 + 打包
- 备选：加载 emsdk 环境后 `emmake make BUILD_TYPE=Release`（`BROTLI=1` 可产出 .br）

### 运行单个测试 / 验证

- 本仓库没有单元测试框架；"单测" = 改动 C++ 后重新构建对应平台产物，再在 HBuilderX 运行 App，进入 `pages/index/index.uvue` 点击「校验 add(1, 2)」应显示 `= 3 通过`。
- 修改 `.uvue`/`.uts` 后，用 HBuilderX（或 uni-agent 的 syntax-checker / verify-compile 能力）做语法与编译校验。
- iOS 构建验证可触发 GitHub Actions `build-ios-uasm`（仅手动 dispatch，Node 22 + macOS）。

## 代码风格与协作规则

### 通用格式（.editorconfig）

- UTF-8、LF 换行、Tab 缩进（indent_size 4）、去行尾空白、文件末尾留一个换行。
- 实际约定：`.uvue`/`.uts`/`.json` 用 Tab；`src/*.cc` 与 `scripts/*.js` 沿用现有 2 空格缩进，保持一致。

### UTS / UTS（前端）

- 页面使用 `<script setup lang="uts">` 组合式 API（参考 pages/index/index.uvue）。
- 类型标注使用 UTS 类型语法（如 `let lib : any = null`），跨端需注意 UTS 与 TS 的差异。
- 异步用 `async/await`；所有可失败操作（加载模块、调用原生函数）必须 try/catch，失败时同时 `console.log(e)` 与更新页面 `errorMessage`。
- 样式只写 class 选择器；`display: flex` 必须显式写 `flex-direction`（uni-app x 默认 column）。

### C++（src/）

- 标准：C++20；原生绑定禁用异常（`-fno-exceptions`），错误用 napi 的 `napi_throw_type_error` + 返回 `nullptr` 处理。
- 匿名命名空间包裹实现细节；函数/变量小驼峰（`Add`、`result`）；每次 N-API 调用都检查返回值是否 `napi_ok`。
- 新增原生 API 时同步修改三处：`binding.cc`（App 端）、`wasm_binding.cc`（Web/小程序端）、`index.d.ts`（根目录与 `uni_modules/test-uasm/uasm/index.d.ts` 两份需保持一致）。

### JavaScript（scripts/）

- 顶部 `'use strict'`；CommonJS；单引号、无分号；错误路径 `console.error` + `process.exit(1)`；日志前缀 `[脚本名]`（如 `[build-wasm]`）。

### JSON / 配置

- `manifest.json`、`pages.json` 允许注释（HBuilderX 约定），保留现有注释风格。
- 构建脚本中的 SDK 路径为机器特定配置，改动需谨慎并确认跨环境可用（优先支持环境变量覆盖）。

### 其他协作约定

- 不要手动编辑 `uni_modules/test-uasm/uasm/` 下的二进制与生成 JS/WASM 产物；一律通过构建命令重新生成。
- 提交前运行相关构建命令确认可编译；提交信息用简短英文或中文一行（现有历史如 `init: uasm test plugin ...`）。
- 涉及原生模块能力（uni-gyp/UASM/uts 插件）时，优先查阅 uni-agent 内置知识库与官方文档，不要臆测 API。
