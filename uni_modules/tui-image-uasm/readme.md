# tui-image-uasm

uni-app x 的图片处理插件（UASM 原生插件）。14 个 API：缩放/裁剪/旋转/翻转/模糊/圆角/圆形裁剪/扩展填充/边缘模糊/渐进模糊/合成/格式转换压缩 + 主色提取 + 信息解析，纯 C++ 实现（stb_image / stb_image_resize2 / stb_image_write），无第三方依赖，原生执行性能。

## 支持平台

| uni-app x App-Android | uni-app x App-iOS | uni-app x App-Harmony | uni-app x Web | 微信小程序 | 支付宝小程序 |
|---|---|---|---|---|---|
| √ | √ | √ | √ | √ | √ |

要求 HBuilderX 5.31+（蒸汽模式）。

## API

所有处理类 API 入参为图片路径（本地路径或网络 URL），`format` 为 `"png" | "jpg"`（默认 png），`quality` 仅 jpg 生效（1-100，默认 85，透明像素自动铺白底）。

### getInfo(path)

解析图片宽高与原始分量数（1=灰度 2=灰度+alpha 3=RGB 4=RGBA）。

```ts
const info = await getInfo('/static/logo.png')
console.log(info.width, info.height, info.channels)
```

### resize(path, width, height, format?, quality?)

缩放图片。`height` 传 0 按宽度等比缩放；`width` 传 0 按高度等比缩放。返回编码后的图片字节。

### crop(path, x, y, width, height, format?, quality?)

裁剪图片，区域自动收敛到图片范围内。

### rotate(path, degrees, format?, quality?)

旋转图片，支持 90/180/270（顺时针，360 整数倍归约）。

### convert(path, format, quality?)

格式转换/压缩：png 无损；jpg 按 quality 压缩。

### getPalette(path, colorCount?)

提取主色、调色板与四边颜色：返回 `{ width, height, dominant, palette, edges }`（dominant 为 `[r,g,b]`，palette 按像素数降序，edges 为 `[上, 右, 下, 左]`）。

### flip(path, horizontal, vertical, format?, quality?)

翻转图片（水平/垂直）。

### blur(path, radius, format?, quality?)

高斯模糊。

### roundCorners(path, radius, format?, quality?)

圆角裁剪。

### circleClip(path, format?, quality?)

圆形裁剪。

### extendFill(path, targetWidth, targetHeight, direction?, centerRatio?, fill?, blurRadius?, format?, quality?)

扩展填充到目标尺寸（如聊天背景/海报底图），支持方向、中心比例、填充色与背景模糊。

### edgeBlur(path, radius, direction?, regionTop?, regionClear?, regionBottom?, transition?, overlay?, overlayOpacity?, format?, quality?)

指定区域边缘模糊（如图片底部渐变模糊托底文字）。

### progressiveBlur(path, direction, radius, offset?, interpolation?, format?, quality?)

渐进式模糊（模糊半径渐变，如状态栏/导航栏背景）。

### composite(basePath, overlayPath, x, y, alpha?, format?, quality?)

图片合成：把 overlay 叠加到 base 的指定位置。

## 限制说明

- 不支持 ICO 格式（stb_image 不含 ICO 解码）
- 全内存操作，超大图片注意内存占用
- 插件不做文件 IO：处理结果为图片字节，保存请配合 uni 文件 API
