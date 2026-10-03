# tui-svg-uasm

uni-app x 的 SVG 渲染插件（UASM 原生插件）。把 SVG 光栅化为 PNG，纯 C++ 实现（nanosvg + nanosvgrast），无第三方依赖，原生执行性能。

## 支持平台

| uni-app x App-Android | uni-app x App-iOS | uni-app x App-Harmony | uni-app x Web | 微信小程序 | 支付宝小程序 |
|---|---|---|---|---|---|
| √ | √ | √ | √ | √ | √ |

要求 HBuilderX 5.31+（蒸汽模式）。

## API

### getInfo(path)

解析 SVG intrinsic 尺寸（viewBox 或 width/height），返回 `{ width, height }`。

```ts
const info = await getInfo('/static/icon.svg')
console.log(info.width, info.height)
```

### render(path, scale)

按等比 scale 渲染为 PNG，返回 PNG 字节。

```ts
const png = await render('/static/icon.svg', 3) // 3 倍渲染
```

### renderSize(path, width, height)

按指定宽高渲染为 PNG；`height` 传 0 时按宽度等比（反之亦然）。非等比目标尺寸时取最小缩放比，内容贴左上、另一方向留透明。

```ts
const png = await renderSize('/static/icon.svg', 96, 0) // 宽 96，高按比例
```

## 限制说明

- 不支持 `<text>` 文字渲染（nanosvg 能力边界）
- 输入 SVG 需为有效 XML（含 `<svg>` 根节点）
- 插件不做文件 IO：渲染结果为 PNG 字节，保存请配合 uni 文件 API
