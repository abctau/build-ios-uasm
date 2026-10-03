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
├── pages/svg/svg.uvue           # tui-svg-uasm 测试页：getInfo/render/renderSize 逐项校验 + 渲染图预览
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
├── src/tui-svg/                 # ★ tui-svg 插件 C++ 源码（SVG 光栅化为 PNG）
│   ├── binding.cc / wasm_binding.cc / svg_core.h/.cc
│   ├── binding.gyp              # target 名 UasmTuiSvgUasm
│   └── vendor/                  # nanosvg.h + nanosvgrast.h（public domain）+ stb_image_write.h
├── src/tui-pdf/                 # ★ tui-pdf 插件 C++ 源码（多图合成 PDF + PDF 信息解析，无外部 PDF 库）
│   ├── binding.cc / wasm_binding.cc / pdf_core.h/.cc
│   ├── binding.gyp              # target 名 UasmTuiPdfUasm
│   └── vendor/                  # miniz.h（deflate）+ stb_image.h（PNG 解码/JPEG info）
├── src/tui-xlsx/                # ★ tui-xlsx 插件 C++ 源码（多 sheet Excel 读写，OOXML + zip）
│   ├── binding.cc / wasm_binding.cc / xlsx_core.h/.cc
│   ├── xlsx_json.h              # 手写 JSON 子集 parser/writer（无异常）
│   ├── xlsx_xml.h               # 手写 XML tokenizer（实体/CDATA/注释）
│   ├── xlsx_zip.h/.cc           # 复制自 tui-zip zip_core（裁 gzip + maxUncompressed 解压炸弹防御）
│   ├── binding.gyp              # target 名 UasmTuiXlsxUasm
│   └── vendor/                  # miniz.h + zip.c/zip.h（kuba--/zip，MIT）
├── src/tui-ffmpeg/              # ★ tui-ffmpeg 插件 C++ 源码（媒体解析+抽帧+B方案转码，链接预编译 ffmpeg 静态库）
│   ├── binding.cc / wasm_binding.cc / ffmpeg_core.h/.cc（内存 AVIO + swscale RGBA + stb PNG）
│   ├── transcode_core.h/.cc     # B 方案转码引擎（runJob JSON 语义 / 统一 demux 分发 / 逻辑位置写 VecWriter）
│   ├── binding.gyp              # target 名 UasmTuiFfmpegUasm；.a 走 ldflags 原样传（libraries 会被加 -l 前缀）
│   └── vendor/ffmpeg/           # include/（官方子目录布局！平铺会让 libavutil/time.h 劫持系统头）+ lib/{arm64-v8a,x86_64,ohos-arm64,ohos-x86_64,wasm}/*.a
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

tui-svg（SVG 光栅化为 PNG，nanosvg + nanosvgrast 单头库）：

- API 3 个：`getInfo（intrinsic 尺寸）/ render（scale 缩放）/ renderSize（指定宽高，h=0 等比）`
- `npm run build:tui-svg:android:release` — Android（arm64 + x86_64）
- `npm run build:tui-svg:harmony:release` — 鸿蒙
- `npm run build:tui-svg:ios:release` — iOS xcframework（通常交给 CI）
- `npm run build:tui-svg:pack:uni-module:release` — module-pack 打包
- `npm run build:tui-svg:wasm:release` / `pack:tui-svg:wasm:release` — WASM 构建/分发
- `npm run build:tui-svg:all` / `build:tui-svg:all:wasm` — 依次执行

tui-pdf（多图合成 PDF + PDF 信息解析，手写 PDF writer 无外部 PDF 库）：

- API 2 个：`imagesToPdf(paths, pageSize?('fit'|'a4'|'a5'|'letter'), orientation?('auto'|'portrait'|'landscape'), margin?(pt))`、`getPdfInfo(path)`、`getPdfInfoByBytes(bytes)`；JPEG 直嵌 DCTDecode（CMYK 走解码 Flate），PNG/其他 stb 解码 → RGB → miniz deflate FlateDecode
- `npm run build:tui-pdf:android:release` — Android（arm64 + x86_64）
- `npm run build:tui-pdf:harmony:release` — 鸿蒙
- `npm run build:tui-pdf:ios:release` — iOS xcframework（通常交给 CI）
- `npm run build:tui-pdf:pack:uni-module:release` — module-pack 打包
- `npm run build:tui-pdf:wasm:release` / `pack:tui-pdf:wasm:release` — WASM 构建/分发
- `npm run build:tui-pdf:all` / `build:tui-pdf:all:wasm` — 依次执行
- PDF 渲染方向（PDF→图片）需要 pdfium，体积大，独立评估未做

tui-xlsx（多 sheet Excel 读写，手写 OOXML 无外部 xlsx 库）：

- API 2 个：`writeXlsx(sheets: {name, rows[][]})` → xlsx bytes（string/number/bool/null，inlineStr 无 sharedStrings）、`readXlsx(bytes)` → 同构 JSON（日期/公式返回原始值，样式不解析）；单条目解压上限 64MB 防解压炸弹；行上限 10 万、列 16384、总单元格 100 万
- `npm run build:tui-xlsx:android:release` — Android（arm64 + x86_64）
- `npm run build:tui-xlsx:harmony:release` — 鸿蒙
- `npm run build:tui-xlsx:ios:release` — iOS xcframework（通常交给 CI）
- `npm run build:tui-xlsx:pack:uni-module:release` — module-pack 打包
- `npm run build:tui-xlsx:wasm:release` / `pack:tui-xlsx:wasm:release` — WASM 构建/分发
- `npm run build:tui-xlsx:all` / `build:tui-xlsx:all:wasm` — 依次执行
- 实现要点：手写 JSON 子集 parser（xlsx_json.h）+ XML tokenizer（xlsx_xml.h），均无异常；xlsx_zip 复制 tui-zip zip_core 后加 `zip_entry_size` 读前防御（kuba zip 的 zip_entry_read 会按声称的未压缩大小 malloc，事后检查太晚）；插件不做文件 IO，保存/加载由调用方用 uni API 完成

tui-ffmpeg（媒体解析+抽帧+B方案转码，链接预编译 ffmpeg 7.1.1 裁剪版静态库，LGPL）：

- API：MVP 2 个 `getMediaInfo(path)`（http 优先 Range 拉头部快速解析）、`extractFrame(path, timeMs, maxWidth)` → PNG bytes；B 方案转码 12 个（对齐 DCloud 插件 16929 子集）：`videoMerge/durationClip/audioClip/audioMerge/videoCrop/videoSpeed/videoFilter/videoCompress/videoEffect/videoPictureInPicture/imageRemoveWatermark`（+getMediaInfo）。平台策略：App=path 进出（native runJob 异步线程）；Web=bytes 进出（同步阻塞，性能弱需标注）；**小程序端不支持转码**（胶水只导出占位抛错，保留解析+抽帧）
- 转码引擎：native 单入口 `runJob(optionsJson)`（napi async work+Promise / embind 同步 bytes），12 API 语义由胶水拼 JSON：inputs（路径数组或占位 ['a','b']）+ output + videoCodec(copy|none|mpeg4) + audioCodec + videoFilter/audioFilter + startMs/durationMs + scaleWidth/scaleHeight + videoBitrateKbps + overlay{x,y,width,height} + mixAudio。视频输出编码只有内置 mpeg4（LGPL 禁 libx264），音频 aac
- B 方案 configure 白名单（三个 build-*.sh 已固化）：`--enable-encoder=aac,mpeg4,png` + `--enable-muxer=mp4,ipod,adts,image2` + `--enable-filter=crop,scale,overlay,format,setpts,hue,eq,null,negate,atempo,amix,volume,aformat,anull,aresample`（**aresample 必须加**，aformat 格式协商隐式依赖，漏了报 "'aresample' filter not present"）+ image2/image2pipe demuxer + png/mjpeg decoder；vendor 每端 6 个 .a（新增 libavfilter/libswresample）
- `npm run build:tui-ffmpeg:android:release` — Android（arm64 + x86_64）
- `npm run build:tui-ffmpeg:harmony:release` — 鸿蒙（链接 vendor/ffmpeg/lib/ohos-arm64、ohos-x86_64 静态库；ffmpeg ohos 构建脚本 E:\ffmpeg-spike\build-harmony.sh）
- `npm run build:tui-ffmpeg:ios:release` — iOS xcframework（**仅 CI/Mac 可构建**，ld 吸收方案）：先跑 `src/tui-ffmpeg/build-ios.sh` 产出 ffmpeg iOS 静态库（configure 走 xcrun clang + SDK sysroot，白名单同其他平台；device arm64 + sim x86_64 各编一次，lipo 成单个 fat `libUasmFfmpegLibs.a`，sim arm64 与 device arm64 二进制兼容不用单独编），binding.gyp 的 ios ldflags 用 `<!(node -p process.cwd())` 拼绝对路径引用该 .a（**不能用 libtool 预合并进 dylib**——40MB+ 的静态合并二进制让 DCloud Windows 签名工具报 "Invalid Macho File (2) Can't Parse"；ld 吸收 + `-dead_strip` 后 dylib 只含用到的符号，体积回落到 MB 级）。gyp 的 `<!(cmd)` 对所有条件块**无条件求值**——命令必须跨平台（用 node 不能用 pwd），否则 Windows 构建其他平台直接炸；.gitignore 排除 ffmpeg-spike-src/ 与 vendor/ffmpeg/lib/ios/（CI 现场从 ffmpeg.org 下载源码现场编）
- `npm run build:tui-ffmpeg:pack:uni-module:release` — module-pack 打包
- `npm run build:tui-ffmpeg:wasm:release` / `pack:tui-ffmpeg:wasm:release` — WASM 构建/分发（wasm .a 在 vendor/ffmpeg/lib/wasm，来自 build-emscripten）
- `npm run build:tui-ffmpeg:all` / `build:tui-ffmpeg:all:wasm` — 依次执行
- 产物：Android .so ~5.5MB/abi、WASM ~4.8MB（brotli 后更小）；ffmpeg 构建方法论（msys 路径转换/CC_IDENT GBK/emconfigure 不可用等 8 坑）见 `E:\ffmpeg-spike\BUILD-NOTES.md`
- 转码测试脚本：`unpackage/test-tui-ffmpeg-transcode.mjs`（10 用例 ALL PASS）+ `unpackage/test-tui-ffmpeg.mjs`（MVP 回归 6 用例）；**wasm 产物是 ENVIRONMENT=web**，Node 测试须用 http server + fetch 加载 .wasm（locateFile 给本地路径会 fetch file:// 失败）；排除法调试脚本 unpackage/debug-job.mjs

WASM（需 emsdk，默认 `E:/emsdk`，可用环境变量 `EMSDK` 覆盖）：- `npm run build:tui-color-thief:wasm:release` — 编译 `src/tui-color-thief/` → `web/release/`
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
- **胶水层 App/小程序端严禁直接用 fetch**（tui-pdf 实录）：iOS 的 vapor JS 运行在 JavaScriptCore，**没有 fetch 全局**，报 "Can't find variable: fetch"（Android 引擎有 polyfill 所以本机测试不易暴露）；http 走 uni.downloadFile、本地/临时路径走 readFile（参照 tui-ffmpeg app-js 的 readBytes）。
- **native fopen 只能读沙盒绝对路径**（tui-ffmpeg path 模式实录）：`/static/`（iOS 在 Bundle、Android 在 assets）与 http 路径直接传给 native runJob 必失败——胶水层必须先把输入落到 `uni.env.USER_DATA_PATH` 临时文件再传绝对路径，输出路径去 `file://` 前缀归一化（runJobToPath 统一处理 inputs/output）。iOS dlopen 报 "symbol not found in flat namespace (_sws_xxx)" = xcframework 二进制里没合并 ffmpeg 静态库——走 build-ios.sh + merge-ios-ffmpeg.js 流程，merge 脚本已带 nm 关键符号自检。
- nanosvg 的 `nsvgParse` **会原地修改输入字符串**（必须传可写、以 \0 结尾的副本，parse 完才能 free）；且对垃圾输入**不返回 null 而是空 image**——必须检查 `image->shapes == nullptr` 判定解析失败（svg_core.cc 的 ParseSvgCopy）。
- nanosvgrast.h 的光栅化头文件名容易猜错：是 `nanosvgrast.h`（不是 nanosvg.raster.h），与 nanosvg.h 同在仓库 src/ 目录。
- `nsvgRasterize` 只支持**单一等比 scale**（无 x/y 独立缩放）：renderSize 非等比时取 min(tx, ty)，内容贴左上、另一方向留透明。
- nanosvg 已在解析期把 viewBox/内容边界回退到 image->width/height（恒 >0），无需自己处理 viewBox 回退；不支持 `<text>` 文字渲染。
- embind **不接受原生 Uint8Array 作为 register_vector<uint8_t> 参数**（要 Uint8Vector 包装类实例）——多字节数组参数一律用 `emscripten::val` + `vecFromJSArray<uint8_t>` 手动转换（tui-pdf 的 packed+offsets 模式）。
- iOS 15 deployment target 下 libc++ 的**浮点 `std::to_chars` 标记为 iOS 16.3+ 可用**（Windows MSVC / Android NDK libc++ 均可用，只有 iOS CI 挂）——double 最短往返格式化统一用 prec 1..17 的 `%.*g` + strtod 往返验证搜索（xlsx_json.h 的 `FormatDoubleShortest`），整数输出无小数点、行为与 to_chars 等价且跨端一致。
- Node 测试脚本同时加载多个 uasm WASM 模块时，`locateFile` 必须给每个模块返回**不同的 URL**（否则 http server 按路由返回错文件，embind 绑定静默错位、导出残缺）。
- 多图传递约定（napi/embind 同构）：胶水层把所有图拼成一个大 Uint8Array + 平铺 offsets `[start0,len0,start1,len1,...]`，绑定层再拆分——避免 napi 遍历对象数组与 embind 嵌套 vector 的跨端差异。
- uni-gyp 的 `libraries` 条目会被加 `-l` 前缀——链接预编译静态库（.a）一律走 `ldflags` 原样传绝对路径（CMake 把 ldflags 输出在 objects 之后，顺序满足单遍解析）；gyp `sources` 里的 .a 会被 CMake 忽略（.so 只有几 KB 且符号全 U，被 `--unresolved-symbols=ignore-all` 掩盖，务必 nm 验证）。
- ffmpeg 7.1 公共头**没有 extern "C" 保护**，且 `libavutil/time.h` 是真实公共头——头文件必须保持官方 `include/{libavformat,libavcodec,libswscale,libavutil}/` 子目录布局（平铺会让 `#include <time.h>` 命中 ffmpeg 的 time.h，tm/nanosleep 未声明 → libc++ locale/pthread 头全面报错），且使用方必须整体包 extern "C"。
- ffmpeg configure 的 `--sysroot` 路径**带空格会被 configure 内部重分词拆掉**（即使 bash 引号正确）——DevEco NDK 在 `C:\Program Files\...`，用 junction `C:\ohos-ndk`（已建）绕过；ohos 交叉编要点：`--target-os=linux`（ohos 是 linux 内核）+ `--cc=clang --extra-cflags="--target=aarch64-linux-ohos -D__MUSL__" --sysroot=C:/ohos-ndk/sysroot` + llvm-ar/nm/ranlib/strip。
- **内存 AVIO 写 muxer 三坑**（tui-ffmpeg 转码调试实录）：① avio_alloc_context **必须传 seek 回调**，否则 `pb->seekable=0`，mov/adts muxer write_header 直接报 "muxer does not support non seekable output"（EINVAL 且难猜）；② 回调必须实现"**逻辑位置写**"语义——seek 回调只更新 write 位置、write 回调按该位置 memcpy 覆盖（纯 append 的 insert 会让 mov trailer 回填 mdat size 的 4 字节 append 到文件尾，产物 mdat sz=0 → demux INVALIDDATA）；③ 收尾**不要用 avio_closep**（CUSTOM_IO 模式下其内部间接调用在 wasm 上 call_indirect 越界，报 "table index is out of bounds"）——正确姿势 `avio_flush + av_freep(&pb->buffer) + avio_context_free`。
- wasm 崩溃 "table index is out of bounds"/"null function or function signature mismatch" = call_indirect 空槽，**不一定是堆损坏**（SAFE_HEAP 干净）——用 emcc `-g2` 产出 name section，Node 异常栈带 `wasm-function[N]:0xoffset` 直接定位到 C++ 函数（本次即定位到 avio_closep）。
- emscripten glue 的 `ENVIRONMENT=web` 在 Node 里**直接 throw "not compiled for this environment"**；Node 测试要么构建时加 `web,node`，要么用 http server 提供 wasm（fetch 拉取，locateFile 不能给本地路径）。
- ffmpeg 转码多流文件**必须用统一 demux 分发循环**（单循环 av_read_frame 按 stream_index 分发给各 StreamPlan）——按流逐个 `while(av_read_frame)` 处理会共享读指针，后处理的流只能拿到前流停止位置之后的包（症状：durationClip 输出时长错、音轨内容错）。
- duration 裁剪到点后要 `avcodec_flush_buffers(dec)` 丢掉解码器缓冲的残留帧，否则输出比 durationMs 多出 1~3s（编码器/滤镜缓冲各存一段）。
- 改 `scripts/*.js` 用 PowerShell `Set-Content -Encoding UTF8` 会**写 BOM**（binding.gyp 带 BOM 让 uni-gyp 报 `SyntaxError: invalid non-printable character U+FEFF`；UTF8 在 PS5.1 默认带 BOM）——写完脚本必须验证无 BOM；node 内联 `node -e "..."` 的引号在 PowerShell 会被反复吞（复现 5+ 次），一律写临时 .js 文件执行。
- ffmpeg configure 的 `--sysroot` 路径**带空格会被 configure 内部重分词拆掉**（即使 bash 引号正确）——DevEco NDK 在 `C:\Program Files\...`，用 junction `C:\ohos-ndk`（已建）绕过；ohos 交叉编要点：`--target-os=linux`（ohos 是 linux 内核）+ `--cc=clang --extra-cflags="--target=aarch64-linux-ohos -D__MUSL__" --sysroot=C:/ohos-ndk/sysroot` + llvm-ar/nm/ranlib/strip。

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

- **iOS uasm 运行时不导出 `napi_create_async_work`**（官方文档"暂不支持 worker/thread-safe 等异步 API"，已与 DCloud 确认后期会补）：tui-ffmpeg 的 runJob 用了 Promise+async work，引用该符号导致 iOS `dlopen` 解析未定义符号失败——**整个插件所有 API 全灭**（不只是异步函数）。**决策（用户）：tui-ffmpeg iOS 支持暂停，binding.cc 保留 async work 原实现不做条件编译，等官方补齐 API 后直接重出 iOS 产物即可**（build-ios.sh + binding.gyp 的 ld 吸收方案已就绪，勿删）；Android/Harmony/Web 不受影响。其他插件 binding 切勿引用 async work 系符号，除非接受 iOS 全灭。若未来需要 iOS 真异步，可选 C++ 分片 session（runJobPrepare/Step/Cancel + JS 时间片驱动）。
- iOS xcframework 的 **simulator slot 不能复用 device arm64 静态库**：Xcode 26 ld 校验 object 内嵌平台元数据（`LC_BUILD_VERSION` platform 字段），device arm64 .o 链 simulator 直接报 "built for 'iOS'"——ffmpeg 按平台产出 `libUasmFfmpegLibs-iphoneos.a` / `-iphonesimulator.a`（sim = arm64+x86_64 fat），binding.gyp 用 `$(PLATFORM_NAME)` build setting 链接期自动选择；gyp `<!(cmd)` 求值 cwd 是 **binding.gyp 所在目录**（非仓库根），拼接路径别重复前缀。
- metartc/WebRTC 方向已放弃（用户决策 2026-10-03：太麻烦，DESIGN.md 归档仅作参考）。
- tui-ffmpeg（规划中）：构建 spike 已完成——ffmpeg 7.1.1 裁剪版（LGPL，解析+抽帧，禁用编码器/网络/滤镜）三端静态库构建通过（Android arm64/x86_64 + emscripten 单线程，各 ~8MB），native 链接解码验证 PROBE OK。完整构建方法论与坑见 `E:\ffmpeg-spike\BUILD-NOTES.md`（msys 路径转换/SHELL/CC_IDENT GBK/CCDEP awk 管道等）；ffmpeg 源码与构建脚本在 `E:\ffmpeg-spike\`（不进本仓库，集成时只进产物）。

- 不要手动编辑 `uni_modules/tui-color-thief-uasm/uasm/` 下的二进制与生成 JS/WASM 产物；一律通过构建命令重新生成。
- 提交前运行相关构建命令确认可编译；提交信息用简短英文或中文一行。
- 涉及原生模块能力（uni-gyp/UASM/uts 插件）时，优先查阅 uni-agent 内置知识库与官方文档，不要臆测 API。
