# AGENTS.md

面向智能编码代理的仓库指南。本项目是 **uni-app x（vapor 模式）宿主应用 + UASM 原生插件 `tui-color-thief-uasm`** 的开发工程：C++ 原生代码经 uni-gyp 编译为各端动态库/框架，Web 与小程序端经 Emscripten 编译为 WASM，统一打包进 `uni_modules/tui-color-thief-uasm`，插件功能是提取图片主色/调色板/四边颜色（median-cut 算法 + stb_image 解码）。

## 项目结构与模块组织

```
├── main.uts / App.uvue          # uni-app x 应用入口
├── manifest.json                # uni-app x 应用清单（vapor: true；Android abiFilters: arm64-v8a, x86_64）
├── pages.json                   # 页面路由配置（pages/index/index 首页入口 + pages/zip/zip zip 测试页）
├── pages/index/index.uvue       # 测试页面：验证 getPalette('/static/logo.png')，跳转 zip 测试页
├── pages/zip/zip.uvue           # tui-zip-uasm 测试页：zip/gzip 六步内存往返逐项校验
├── pages/image/image.uvue       # tui-image-uasm 测试页：getInfo/resize/convert/getPalette 逐项校验
├── src/tui-color-thief/         # ★ 插件 C++ 源码
│   ├── binding.cc               # N-API 绑定（Android/iOS/Harmony 共用，C++20，禁用异常）
│   ├── binding.gyp              # uni-gyp 构建配置（target 名 UasmTuiColorThiefUasm）
│   ├── color_thief.h/.cc        # 核心逻辑：stb_image 解码 + median-cut 量化 + 四边取色
│   ├── wasm_binding.cc          # Emscripten 绑定（embind，仅 Web/小程序）
│   └── stb/stb_image.h          # vendored stb_image（public domain）
├── src/tui-zip/                 # ★ tui-zip 插件 C++ 源码（zip 打包/解压 + gzip）
│   ├── binding.cc / wasm_binding.cc / zip_core.h/.cc
│   ├── binding.gyp              # target 名 UasmTuiZipUasm
│   └── vendor/                  # kuba--/zip（MIT）+ miniz amalgamation
├── src/tui-image/               # ★ tui-image 插件 C++ 源码（缩放/裁剪/旋转/转格式 + 吸收 color-thief 主色提取）
│   ├── binding.cc / wasm_binding.cc / image_core.h/.cc
│   ├── binding.gyp              # target 名 UasmTuiImageUasm
│   └── stb/                     # stb_image.h + stb_image_resize2.h + stb_image_write.h（IMPLEMENTATION 全在 image_core.cc）
├── scripts/
│   ├── build-wasm-tui-color-thief.js   # 不依赖 make 的 WASM 构建脚本（需 EMSDK）
│   └── pack-wasm-tui-color-thief.js    # 将 WASM 产物分发打包到 uni_modules
│   ├── build-wasm-tui-zip.js           # tui-zip WASM 构建脚本（C++/C 分步编译再链接）
│   └── pack-wasm-tui-zip.js            # tui-zip WASM 产物分发打包到 uni_modules
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
├── uni_modules/tui-zip-uasm/    # ★ tui-zip 插件（结构与 color-thief 相同：package.json/utssdk 四平台/uasm 产物）
│   └── uasm/index.d.ts          # TuiZipUasm：zipCreate/zipList/zipReadEntry/zipExtractAll/gzipCompress/gzipDecompress
├── web/release/                 # WASM 构建中间产物
├── static/                      # 静态资源（logo 等测试图片）
├── Makefile                     # emscripten 官方构建流程备选（emmake make）
└── .github/workflows/build-ios.yml  # 手动触发的 iOS 构建流水线（workflow_dispatch；矩阵并行构建所有 tui-* 插件，inputs.plugins 可过滤）
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

tui-zip（zip 打包/解压 + gzip，vendor/zip.c 依赖 `ZIP_HAVE_SYMLINK=1`）：

- `npm run build:tui-zip:android:release` — Android（arm64 + x86_64）
- `npm run build:tui-zip:harmony:release` — 鸿蒙
- `npm run build:tui-zip:ios:release` — iOS xcframework（通常交给 CI）
- `npm run build:tui-zip:pack:uni-module:release` — module-pack 打包
- `npm run build:tui-zip:wasm:release` / `pack:tui-zip:wasm:release` — WASM 构建/分发（C++ 与 vendor C 分步编译再链接）
- `npm run build:tui-zip:all` / `build:tui-zip:all:wasm` — 依次执行

tui-image（缩放/裁剪/旋转/翻转/模糊/圆角/扩展填充/合成/格式转换 + 主色提取，stb 头文件进 image_core.cc 单编译单元）：

- API 14 个：`getInfo / resize / crop / rotate(任意角度) / convert / getPalette / flip / blur / roundCorners / circleClip / extendFill / edgeBlur / progressiveBlur / composite`
- `npm run build:tui-image:android:release` — Android（arm64 + x86_64）
- `npm run build:tui-image:harmony:release` — 鸿蒙
- `npm run build:tui-image:ios:release` — iOS xcframework（通常交给 CI）
- `npm run build:tui-image:pack:uni-module:release` — module-pack 打包
- `npm run build:tui-image:wasm:release` / `pack:tui-image:wasm:release` — WASM 构建/分发
- `npm run build:tui-image:all` / `build:tui-image:all:wasm` — 依次执行

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
- `uasm/index.d.ts` 必须用 `export class <Pascal名>` 形式声明原生 API（类名 = 产物名去掉 Uasm 前缀后的 Pascal 名，如 `TuiColorThiefUasm`），云端编译据此生成 Kotlin 绑定类；用 `export = plugin` 形式会导致打基座报 "Unresolved reference"。d.ts 中自定义 interface 返回类型与嵌套数组（`number[][]`）已验证可用。
- App 端入口放 `utssdk/app-js/index.uts`（优先级高于 index.uts），参考官方 uni-sqlite 插件结构；`readFileSync` 编译为 Kotlin 时 encoding 参数无默认值会报 "No value passed"，App 端用异步 `readFile` 代替。
- UASM 的 Web/小程序入口 JS **必须与 uni_modules 插件目录同名**：插件 `tui-zip-uasm` 的入口必须是 `uasm/web/tui-zip-uasm.js`，否则编译报"无法加载 uasm 插件…请确认插件路径正确"。构建/打包脚本中的产物名、Makefile 的 `APP` 都遵循该规则。
- `uni-gyp module-pack --target <kebab-名>` 会按规则 `Uasm` + PascalCase 推导产物名（如 `tui-zip-uasm` → `UasmTuiZipUasm`），binding.gyp 的 `target_name` 必须与该推导一致，否则 module-pack 报 "No build products found"。
- 新增原生 API 时同步修改三处：`binding.cc`（App 端）、`wasm_binding.cc`（Web/小程序端）、`uasm/index.d.ts`。
- UASM 原生函数可接收 `Uint8Array | ArrayBuffer`（napi/embind 均支持）；返回二进制用 `napi_create_external_arraybuffer` + `napi_create_typedarray`（malloc 分配，finalize 回调 free）；embind 侧用 `typed_memory_view` + `Uint8Array.set`。
- UASM 的 Web 端入口 JS 必须与插件目录同名（见上文），且 WASM 产物经 `uni.loadUasm` 在浏览器加载后行为与直接 import ESM 等价；调试 WASM 可用 Node 直调（`createXxxModule({ locateFile })` + 本地 http server 提供 .wasm，Node 的 fetch 不支持 file://）。
- stb_image 不支持 ICO/ICO 内嵌位图格式，传 .ico 会报 "failed to decode image"（Web 端测试时最容易踩）；诊断可看 wasm_binding.cc 抛出的 bytes/head 信息。
- UTS 的 `number.toRadix()` 在 web 端（vapor 编译为 JS）不存在，跨端进制转换用手写查表实现（见 utssdk/index.uts 的 toHexPart）。
- 混编 C 静态库源码（如 vendor/zip.c）进 uni-gyp：configure 类宏必须显式定义（zip.c 依赖 `-DZIP_HAVE_SYMLINK=1` 才会 include `<unistd.h>`，否则 ftruncate/symlink/unlink 编译失败）；binding.gyp 的 `defines` 或 wasm 脚本参数里补齐。
- em++ 会把 `.c` 当 C++ 编译（`-x c` 不可与 `-std=c++20` 全局混用）；分步编译（.cc 一次、.c 一次）再链接最稳，见 scripts/build-wasm-tui-zip.js。
- 单头 amalgamation 库（miniz.h 声明+实现一体、符号非 static）只能在一个编译单元 include；其他 TU 需要 API 时用 extern "C" 重新声明（照抄结构体布局，见 src/tui-zip/zip_core.cc）。
- stb 系列单头库的 `STB_IMAGE_IMPLEMENTATION` / `STB_IMAGE_RESIZE_IMPLEMENTATION` / `STB_IMAGE_WRITE_IMPLEMENTATION` 必须全部定义在同一个编译单元（src/tui-image/image_core.cc），binding 层只 include image_core.h，避免实现重复。
- `stbi_write_jpg` 无 alpha 通道：输出 JPEG 前需把半透明像素铺白底合成（image_core.cc 的 FlattenAlphaToWhite），否则 alpha 被当颜色通道写坏。
- resize2 的 sRGB 接口：`stbir_resize_uint8_srgb(in, w, h, stride, out, tw, th, stride, STBIR_RGBA)`，out 传 nullptr 自动分配；测试静态图若是 Adam7 隔行 PNG，自写的参考解码器必须处理 interlace，否则像素级比对会误报（stb 自动处理，以 stb 为准）。
- embind 的 number 参数传了数组等错类型时会被转成 **NaN 而不是抛错**（napi 会抛），调用方拿 NaN 参与计算就静默失效——C 层对浮点参数做 `std::isfinite` 防御（image_core.cc EdgeBlurImage 的 regions），测试脚本的参数个数/类型要与 wasm 绑定签名严格一致。
- uni_modules 插件的 package.json 若带 UTF-8 BOM，vite dev 按需加载时报 `"?{\n..." is not valid JSON`（loadPackageData），页面首次 import 插件才触发，容易漏查；node 脚本生成/复制 JSON 后要验证无 BOM。
- 胶水层 4 平台文件（web/app-js/mp-weixin/mp-alipay）除 `readImageBytes` 外完全同构：web 用 fetch，其余三端用 downloadFile+getFileSystemManager().readFile（参考 tui-color-thief app-js 实现）；批量同步时只替换 `export async function getInfo` 之后的公共段，头部 readImageBytes 各自保留。

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
