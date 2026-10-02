'use strict'

// Build the tui-svg WASM artifact (same flow as build-wasm-tui-image.js,
// targeting src/tui-svg/wasm_binding.cc + svg_core.cc).
// Override the SDK location with the EMSDK environment variable.

const { spawnSync } = require('child_process')
const fs = require('fs')
const path = require('path')

const root = path.resolve(__dirname, '..')
const emsdk = process.env.EMSDK || 'E:/emsdk'
const isWin = process.platform === 'win32'

const emscriptenDir = path.join(emsdk, 'upstream', 'emscripten')
const llvmBin = path.join(emsdk, 'upstream', 'bin')

function findVersionDir(base, fileName) {
  if (!fs.existsSync(base)) return ''
  for (const entry of fs.readdirSync(base)) {
    const full = path.join(base, entry, fileName)
    if (fs.existsSync(full)) return path.join(base, entry)
  }
  return ''
}

const nodeDir = process.env.EMSDK_NODE || findVersionDir(path.join(emsdk, 'node'), isWin ? 'node.exe' : 'node')
const pythonDir = process.env.EMSDK_PYTHON || findVersionDir(path.join(emsdk, 'python'), isWin ? 'python.exe' : 'python3')

const empp = path.join(emscriptenDir, isWin ? 'em++.exe' : 'em++')
if (!fs.existsSync(empp)) {
  console.error('[build-wasm-tui-svg] em++ not found: ' + empp)
  console.error('[build-wasm-tui-svg] set EMSDK to your emsdk install dir (default: E:/emsdk)')
  process.exit(1)
}

const outDir = path.join(root, 'web', 'release')
fs.mkdirSync(outDir, { recursive: true })
// The web entry must be named after the uni_modules plugin directory:
// uni_modules/tui-svg-uasm/uasm/web/tui-svg-uasm.js
const outJs = path.join(outDir, 'tui-svg-uasm.js')

const args = [
  path.join(root, 'src', 'tui-svg', 'wasm_binding.cc'),
  path.join(root, 'src', 'tui-svg', 'svg_core.cc'),
  '-I', path.join(root, 'src', 'tui-svg'),
  '-std=c++20',
  '-o', outJs,
  '--no-entry',
  '-lembind',
  '-s', 'WASM=1',
  '-s', 'ALLOW_MEMORY_GROWTH=1',
  '-s', 'EXPORTED_FUNCTIONS=["_malloc","_free"]',
  '-s', 'DYNAMIC_EXECUTION=0',
  '-O3',
  '-s', 'MODULARIZE=1',
  '-s', 'EXPORT_ES6=1',
  '-s', 'ENVIRONMENT=web',
  '-s', 'EXPORT_NAME=createTuiSvgUasmModule',
]

const env = Object.assign({}, process.env, {
  EM_CONFIG: path.join(emsdk, '.emscripten'),
  EMSDK: emsdk,
  PATH: [emsdk, emscriptenDir, nodeDir, pythonDir, llvmBin, process.env.PATH].join(path.delimiter),
})

console.log('[build-wasm-tui-svg] ' + empp)
console.log('[build-wasm-tui-svg] ' + args.join(' '))

const result = spawnSync(empp, args, { stdio: 'inherit', env, cwd: root })
if (result.error) {
  console.error(result.error.message)
  process.exit(1)
}
process.exit(result.status === null ? 1 : result.status)
