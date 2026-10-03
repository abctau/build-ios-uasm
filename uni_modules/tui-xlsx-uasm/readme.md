# tui-xlsx-uasm

uni-app x 的 Excel 读写插件（UASM 原生插件）。多 sheet xlsx 读写，纯 C++ 实现（手写 OOXML + zip），无第三方依赖，原生执行性能。

## 支持平台

| uni-app x App-Android | uni-app x App-iOS | uni-app x App-Harmony | uni-app x Web | 微信小程序 | 支付宝小程序 |
|---|---|---|---|---|---|
| √ | √ | √ | √ | √ | √ |

要求 HBuilderX 5.31+（蒸汽模式）。

## API

### writeXlsx(sheets)

生成 xlsx 文件字节流。

```ts
import { writeXlsx, XlsxSheet } from '@/uni_modules/tui-xlsx-uasm'

const sheets : XlsxSheet[] = [
	{
		name: 'Sheet1',
		rows: [
			['姓名', '年龄', '已婚', '备注'],
			['张三', 28, true, null],
		],
	},
	{
		name: '统计',
		rows: [['合计', 28, null, 'hello']],
	},
]
const bytes = writeXlsx(sheets)
```

- 单元格值支持 `string | number | boolean | null`（null 写空单元格）
- 返回 `ArrayBuffer`，保存文件请配合 `uni.getFileSystemManager().writeFile` 等平台 API

### readXlsx(bytes)

解析 xlsx 文件字节流，返回与 writeXlsx 输入同构的结构。

```ts
import { readXlsx } from '@/uni_modules/tui-xlsx-uasm'

const wb = readXlsx(bytes)
for (const sheet of wb.sheets) {
	console.log(sheet.name, sheet.rows)
}
```

## 限制说明

- 日期/公式单元格返回原始值（不做格式化解析）；样式不解析
- 行上限 10 万，列上限 16384，总单元格上限 100 万
- 内置解压炸弹防御（单条目解压上限 64MB）
- 插件不做文件 IO：文件读写由调用方用 uni API 完成（读入 `ArrayBuffer`，写出保存）

## 依赖

无第三方依赖（OOXML 与 zip 均为内置 C++ 实现）。

## 推荐插件：TuiPlus Vapor 组件库

[TuiPlus Vapor](https://ext.dcloud.net.cn/plugin?id=28497) 专注 uni-app x 5.0 蒸汽模式的高性能 UI 组件库：内置 TuiVitex 编译器（简单组件编译为官方组件、复杂组件 Canvas 绘制）与 TuiEntine Canvas Flex 渲染引擎，蒸汽模式 + external-class + CSS 原子化最佳实践。

👉 [https://ext.dcloud.net.cn/plugin?id=28497](https://ext.dcloud.net.cn/plugin?id=28497)
