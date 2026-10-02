# AGENTS.md

面向智能编码代理的仓库指南。本项目是 **uni-app x（vapor 模式）宿主应用 + UASM 原生插件 `tui-color-thief-uasm`** 的开发工程：C++ 原生代码经 uni-gyp 编译为各端动态库/框架，Web 与小程序端经 Emscripten 编译为 WASM，统一打包进 `uni_modules/tui-color-thief-uasm`，插件功能是提取图片主色/调色板/四边颜色（median-cut 算法 + stb_image 解码）。

## 项目结构与模块组织

```
├── main.uts / App.uvue          # uni-app x 应用入口
├── manifest.json                # uni-app x 应用清单（vapor: true；Android abiFilters: arm64-v8a, x86_64）
├── pages.json                   # 页面路由配置（仅 pages/index/index）
├── pages/index/index.uvue       # 测试页面：验证 getPalette('/static/logo.png')
├── src/tui-color-thief/         # ★ 插件 C++ 源码
│   ├── binding.cc               # N-API 绑定（Android/iOS/Harmony 共用，C++20，禁用异常）
│   ├── binding.gyp              # uni-gyp 构建配置（target 名 UasmTuiColorThiefUasm）
│   ├── color_thief.h/.cc        # 核心逻辑：stb_image 解码 + median-cut 量化 + 四边取色
│   ├── wasm_binding.cc          # Emscripten 绑定（embind，仅 Web/小程序）
│   └── stb/stb_image.h          # vendored stb_image（public domain）
├── scripts/
│   ├── build-wasm-tui-color-thief.js   # 不依赖 make 的 WASM 构建脚本（需 EMSDK）
│   └── pack-wasm-tui-color-thief.js    # 将 WASM 产物分发打包到 uni_modules
├── uni_modules/tui-color-thief-uasm/
│   ├── package.json             # 插件元信息（type: uasm，打基座必需）
│   ├── utssdk/web/index.uts     # ★ Web 端入口：fetch 读图 + loadUasm
│   ├── utssdk/app-js/index.uts  # ★ App 端入口：downloadFile + readFile + loadUasm
│   ├── utssdk/mp-weixin/ mp-alipay/index.uts  # 小程序端入口：downloadFile + readFile（与 app-js 同构）
│   ├── utssdk/interface.uts     # ColorThiefResult 类型定义（各平台目录共享）
│   └── uasm/                    # 预构建产物（勿手改，由构建命令生成）
│       ├── index.d.ts           # 原生插件类型声明（getPalette(bytes, colorCount?)）
│       ├── app-android/libs/    # arm64-v8a、x86_64 的 .so
│       ├── app-harmony/libs/    # arm64-v8a、x86_64 的 .so
│       ├── app-ios/frameworks/  # UasmTuiColorThiefUasm.xcframework（由 CI 构建）
│       ├── web/                 # tui-color-thief.js + .wasm
│       └── mp-weixin/ mp-alipay/  # tui-color-thief.js + brotli 压缩的 .wasm.br
├── web/release/                 # WASM 构建中间产物
├── static/                      # 静态资源（logo 等测试图片）
├── Makefile                     # emscripten 官方构建流程备选（emmake make）
└── .github/workflows/build-ios.yml  # 手动触发的 iOS 构建流水线（workflow_dispatch）
```

- 无自动化测试目录/框架；验证方式是运行 App/Web 后在测试页点击按钮，或使用 HBuilderX 编译校验。
- `unpackage/`、`node_modules/`、`build/` 均为生成物，不要提交或编辑。

## 构建 / 打包命令

原生各端构建（产物落到 `uni_modules/tui-color-thief-uasm/uasm/`，依赖本机 SDK 路径，package.json 中硬编码）：

- `npm run build:tui-color-thief:android:release` — Android（需 E:/hbAndroidSdk + NDK）
- `npm run build:tui-color-thief:harmony:release` — 鸿蒙（需 DevEco NDK + cmake）
- `npm run build:tui-color-thief:ios:release` — iOS xcframework（arm64, x64，通常交给 CI）
- `npm run build:tui-color-thief:pack:uni-module:release` — `uni-gyp module-pack` 打包 uni_modules
- `npm run build:tui-color-thief:all` — 依次执行上述四步

WASM（需 emsdk，默认 `E:/emsdk`，可用环境变量 `EMSDK` 覆盖）：

- `npm run build:tui-color-thief:wasm:release` — 编译 `src/tui-color-thief/` → `web/release/`
- `npm run pack:tui-color-thief:wasm:release` — 分发到 `uasm/{web,mp-weixin,mp-alipay}`（小程序端自动 brotli 压缩）
- `npm run build:tui-color-thief:all:wasm` — 构建 + 打包
- 备选：加载 emsdk 环境后 `emmake make BUILD_TYPE=Release`（`BROTLI=1` 可产出 .br）

### 运行单个测试 / 验证

- 本仓库没有单元测试框架；"单测" = 改动 C++ 后重新构建对应平台产物，再运行工程进入 `pages/index/index.uvue` 点击「提取图片主色」，应输出主色/四边/调色板 hex。
- 修改 `.uvue`/`.uts` 后，用 HBuilderX（或 uni-agent 的 syntax-checker / verify-compile 能力）做语法与编译校验。
- iOS 构建验证可触发 GitHub Actions `build-ios-uasm`（仅手动 dispatch，Node 22 + macOS）。

## 代码风格与协作规则

### 通用格式（.editorconfig）

- UTF-8、LF 换行、Tab 缩进（indent_size 4）、去行尾空白、文件末尾留一个换行。
- 实际约定：`.uvue`/`.uts`/`.json` 用 Tab；`src/**/*.cc/.h` 与 `scripts/*.js` 沿用现有 2 空格缩进，保持一致。

### UTS（前端）

- 页面使用 `<script setup lang="uts">` 组合式 API（参考 pages/index/index.uvue）。
- 类型标注使用 UTS 类型语法（如 `let lib : any = null`），跨端需注意 UTS 与 TS 的差异。
- 异步用 `async/await`；所有可失败操作必须 try/catch，失败时同时 `console.log(e)` 与更新页面 `errorMessage`。
- 样式只写 class 选择器；`display: flex` 必须显式写 `flex-direction`（uni-app x 默认 column）。
- 插件封装层（utssdk/index.uts）负责吸收平台差异（条件编译），禁止把条件编译泄漏到业务页面。

### UASM 插件（uni-gyp/UASM 特有约定，踩坑总结）

- `uni.loadUasm('uni_modules/xxx')` 的参数**必须是字符串字面量**，不能传 const 变量，否则编译报错。
- uni_modules 插件目录必须包含 `package.json`（`"type": "uasm"`，含 dcloudext/uni_modules 元信息），否则打包自定义基座时报 "Cannot find module .../package.json"；缺失时 HBuilderX 普通运行可能不报错，容易被漏掉。
- `uasm/index.d.ts` 必须用 `export class <Pascal名>` 形式声明原生 API（类名 = 产物名去掉 Uasm 前缀后的 Pascal 名，如 `TuiColorThiefUasm`），云端编译据此生成 Kotlin 绑定类；用 `export = plugin` 形式会导致打基座报 "Unresolved reference"。
- App 端入口放 `utssdk/app-js/index.uts`（优先级高于 index.uts），参考官方 uni-sqlite 插件结构；`readFileSync` 编译为 Kotlin 时 encoding 参数无默认值会报 "No value passed"，App 端用异步 `readFile` 代替。
- UASM 的 Web/小程序入口 JS **必须与 uni_modules 插件目录同名**：插件 `tui-color-thief-uasm` 的入口必须是 `uasm/web/tui-color-thief-uasm.js`，否则编译报"无法加载 uasm 插件…请确认插件路径正确"。构建/打包脚本中的产物名、Makefile 的 `APP` 都遵循该规则。
- `uni-gyp module-pack --target <kebab-名>` 会按规则 `Uasm` + PascalCase 推导产物名（如 `tui-color-thief-uasm` → `UasmTuiColorThiefUasm`），binding.gyp 的 `target_name` 必须与该推导一致，否则 module-pack 报 "No build products found"。
- 新增原生 API 时同步修改三处：`binding.cc`（App 端）、`wasm_binding.cc`（Web/小程序端）、`uasm/index.d.ts`。
- UASM 原生函数可接收 `Uint8Array | ArrayBuffer`（napi/embind 均支持）；WASM 端无法读文件路径，一律由前端读成二进制再传入。
- UASM 的 Web 端入口 JS 必须与插件目录同名（见上文），且 WASM 产物经 `uni.loadUasm` 在浏览器加载后行为与直接 import ESM 等价；调试 WASM 可用 Node 直调（`createXxxModule({ locateFile })` + 本地 http server 提供 .wasm，Node 的 fetch 不支持 file://）。
- stb_image 不支持 ICO/ICO 内嵌位图格式，传 .ico 会报 "failed to decode image"（Web 端测试时最容易踩）；诊断可看 wasm_binding.cc 抛出的 bytes/head 信息。
- UTS 的 `number.toRadix()` 在 web 端（vapor 编译为 JS）不存在，跨端进制转换用手写查表实现（见 utssdk/index.uts 的 toHexPart）。

### C++（src/）

- 标准：C++20；原生绑定禁用异常（`-fno-exceptions`），错误用 napi 的 `napi_throw_type_error` + 返回 `nullptr` 处理。
- 匿名命名空间包裹实现细节；函数/变量小驼峰；每次 N-API 调用都检查返回值是否 `napi_ok`。
- stb_image 的 `STB_IMAGE_IMPLEMENTATION` 只在 color_thief.cc 定义一次。

### JavaScript（scripts/）

- 顶部 `'use strict'`；CommonJS；单引号、无分号；错误路径 `console.error` + `process.exit(1)`；日志前缀 `[脚本名]`。

### JSON / 配置

- `manifest.json`、`pages.json` 允许注释（HBuilderX 约定），保留现有注释风格。
- 构建脚本中的 SDK 路径为机器特定配置，改动需谨慎并确认跨环境可用（优先支持环境变量覆盖）。

### 其他协作约定

- 不要手动编辑 `uni_modules/tui-color-thief-uasm/uasm/` 下的二进制与生成 JS/WASM 产物；一律通过构建命令重新生成。
- 提交前运行相关构建命令确认可编译；提交信息用简短英文或中文一行。
- 涉及原生模块能力（uni-gyp/UASM/uts 插件）时，优先查阅 uni-agent 内置知识库与官方文档，不要臆测 API。
