# tui-zip-uasm

uni-app x 的压缩解压插件（UASM 原生插件）。ZIP 打包/解压 + gzip 压缩解压，纯内存操作，纯 C++ 实现（miniz + kuba--/zip），无第三方依赖，原生执行性能。

## 支持平台

| uni-app x App-Android | uni-app x App-iOS | uni-app x App-Harmony | uni-app x Web | 微信小程序 | 支付宝小程序 |
|---|---|---|---|---|---|
| √ | √ | √ | √ | √ | √ |

要求 HBuilderX 5.31+（蒸汽模式）。

## API

### zipCreate(names, contents)

从内存数据打包 ZIP。

```ts
import { zipCreate } from '@/uni_modules/tui-zip-uasm'

const zipBytes = await zipCreate(
	['a.txt', 'dir/b.json'],
	[new Uint8Array(1, 2, 3), new TextEncoder().encode('{"ok":true}')]
)
```

### zipList(zipBytes)

列出 ZIP 内条目名（不含目录条目）。

```ts
const names = await zipList(zipBytes)
```

### zipReadEntry(zipBytes, name)

读取 ZIP 内指定条目内容。

```ts
const data = await zipReadEntry(zipBytes, 'a.txt')
```

### zipExtractAll(zipBytes)

解压全部条目，返回 `names`（条目名）与 `datas`（一一对应的内容）。

```ts
const { names, datas } = await zipExtractAll(zipBytes)
```

### gzipCompress(data, level?) / gzipDecompress(data)

gzip 压缩/解压，`level` 可选（默认压缩级别）。

```ts
const gz = await gzipCompress(new TextEncoder().encode('hello'))
const raw = await gzipDecompress(gz)
```

## 限制说明

- 全内存操作，超大文件注意内存占用
- 插件不做文件 IO：文件读写由调用方用 uni API 完成（读入/写出 `Uint8Array`）
