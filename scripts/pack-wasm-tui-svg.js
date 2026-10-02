'use strict'

// Distribute the tui-svg WASM artifacts into uni_modules.
// Web uses the raw wasm; mini programs get a brotli-compressed wasm.

const fs = require('fs')
const path = require('path')
const zlib = require('zlib')

const root = path.resolve(__dirname, '..')
const src = path.join(root, 'web', 'release')
const dest = path.join(root, 'uni_modules', 'tui-svg-uasm', 'uasm')

const APP_JS = 'tui-svg-uasm.js'
const APP_WASM = 'tui-svg-uasm.wasm'
const APP_WASM_BR = 'tui-svg-uasm.wasm.br'

function copy(from, to) {
  fs.mkdirSync(path.dirname(to), { recursive: true })
  fs.copyFileSync(from, to)
}

const srcJs = path.join(src, APP_JS)
const srcWasm = path.join(src, APP_WASM)
if (!fs.existsSync(srcJs) || !fs.existsSync(srcWasm)) {
  console.error('[pack-wasm-tui-svg] missing artifacts in ' + src + '; run build:tui-svg:wasm:release first')
  process.exit(1)
}

for (const dir of ['web', 'mp-weixin', 'mp-alipay']) {
  copy(srcJs, path.join(dest, dir, APP_JS))
}
copy(srcWasm, path.join(dest, 'web', APP_WASM))

const brotli = zlib.brotliCompressSync(fs.readFileSync(srcWasm), {
  params: {
    [zlib.constants.BROTLI_PARAM_QUALITY]: 11,
  },
})
for (const dir of ['mp-weixin', 'mp-alipay']) {
  const target = path.join(dest, dir, APP_WASM_BR)
  fs.mkdirSync(path.dirname(target), { recursive: true })
  fs.writeFileSync(target, brotli)
}

console.log('[pack-wasm-tui-svg] packed into uni_modules/tui-svg-uasm/uasm/{web,mp-weixin,mp-alipay}')
