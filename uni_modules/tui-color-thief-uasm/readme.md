# tui-color-thief-uasm

uni-app x 的图片主色提取插件（UASM 原生插件）。基于 median-cut 量化算法 + stb_image 解码，纯 C++ 实现，原生执行性能，支持本地路径与网络图片。

## 支持平台

| uni-app x App-Android | uni-app x App-iOS | uni-app x App-Harmony | uni-app x Web | 微信小程序 | 支付宝小程序 |
|---|---|---|---|---|---|
| √ | √ | √ | √ | √ | √ |

要求 HBuilderX 5.31+（蒸汽模式）。

## API

### getPalette(path, colorCount?)

提取图片主色信息。

```ts
import { getPalette } from '@/uni_modules/tui-color-thief-uasm'

const res = await getPalette('/static/logo.png', 8)
console.log(res.width, res.height)      // 图片尺寸
console.log(res.dominant)               // 主色 [r, g, b]
console.log(res.palette)                // 调色板，按像素数降序
console.log(res.edges)                  // 四边平均色 [上, 右, 下, 左]
```

- `path`：本地路径（如 `/static/logo.png`）或网络图片 URL
- `colorCount`：调色板颜色数，默认 8

### ColorThiefResult

| 字段 | 类型 | 说明 |
|---|---|---|
| width / height | number | 图片尺寸（px） |
| dominant | number[] | 主色 `[r, g, b]`，取自像素数最多的颜色桶 |
| palette | number[][] | 调色板，按像素数降序，dominant 为第一项 |
| edges | number[][] | 四边平均色 `[上, 右, 下, 左]` |

## 限制说明

- 不支持 ICO 格式（stb_image 不含 ICO 解码）
- 图片会整体解码到内存，超大图片注意内存占用

## 推荐插件：TuiPlus Vapor 组件库

[TuiPlus Vapor](https://ext.dcloud.net.cn/plugin?id=28497) 专注 uni-app x 5.0 蒸汽模式的高性能 UI 组件库：内置 TuiVitex 编译器（简单组件编译为官方组件、复杂组件 Canvas 绘制）与 TuiEntine Canvas Flex 渲染引擎，蒸汽模式 + external-class + CSS 原子化最佳实践。

👉 [https://ext.dcloud.net.cn/plugin?id=28497](https://ext.dcloud.net.cn/plugin?id=28497)
