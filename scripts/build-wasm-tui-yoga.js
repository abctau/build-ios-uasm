'use strict'

// Build the tui-yoga WASM artifact (same flow as build-wasm-tui-xlsx.js,
// targeting src/tui-yoga/wasm_binding.cc + yoga_core.cc + vendor/yoga/**.cpp).
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
  console.error('[build-wasm-tui-yoga] em++ not found: ' + empp)
  console.error('[build-wasm-tui-yoga] set EMSDK to your emsdk install dir (default: E:/emsdk)')
  process.exit(1)
}

const outDir = path.join(root, 'web', 'release')
fs.mkdirSync(outDir, { recursive: true })
// The web entry must be named after the uni_modules plugin directory:
// uni_modules/tui-yoga-uasm/uasm/web/tui-yoga-uasm.js
const outJs = path.join(outDir, 'tui-yoga-uasm.js')

const yogaDir = path.join(root, 'src', 'tui-yoga', 'vendor', 'yoga')
const yogaCpps = []
;(function walk(dir) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name)
    if (entry.isDirectory()) walk(full)
    else if (entry.name.endsWith('.cpp')) yogaCpps.push(full)
  }
})(yogaDir)
yogaCpps.sort()

const commonIncludes = ['-I', path.join(root, 'src', 'tui-yoga'), '-I', path.join(root, 'src', 'tui-yoga', 'vendor')]
const commonFlags = [
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
  '-s', 'EXPORT_NAME=createTuiYogaUasmModule',
]

function run(emppArgs, label) {
  console.log('[build-wasm-tui-yoga:' + label + ']')
  const r = spawnSync(empp, emppArgs, { stdio: 'inherit', env, cwd: root })
  if (r.error) {
    console.error(r.error.message)
    process.exit(1)
  }
  if (r.status !== 0) {
    process.exit(r.status === null ? 1 : r.status)
  }
}

const env = Object.assign({}, process.env, {
  EM_CONFIG: path.join(emsdk, '.emscripten'),
  EMSDK: emsdk,
  PATH: [emsdk, emscriptenDir, nodeDir, pythonDir, llvmBin, process.env.PATH].join(path.delimiter),
})

const objs = []

const bindingOut = path.join(outDir, 'tui-yoga-binding.o')
run([
  path.join(root, 'src', 'tui-yoga', 'wasm_binding.cc'),
  ...commonIncludes,
  '-std=c++20', '-fno-exceptions', '-c', '-o', bindingOut,
], 'wasm_binding.cc')
objs.push(bindingOut)

const coreOut = path.join(outDir, 'tui-yoga-core.o')
run([
  path.join(root, 'src', 'tui-yoga', 'yoga_core.cc'),
  ...commonIncludes,
  '-std=c++20', '-fno-exceptions', '-c', '-o', coreOut,
], 'yoga_core.cc')
objs.push(coreOut)

yogaCpps.forEach((cpp, i) => {
  const out = path.join(outDir, 'tui-yoga-vendor-' + i + '.o')
  run([cpp, ...commonIncludes, '-std=c++20', '-O2', '-c', '-o', out], 'vendor ' + path.basename(cpp))
  objs.push(out)
})

const linkArgs = [...objs, '-o', outJs, ...commonFlags]
console.log('[build-wasm-tui-yoga:link]')
const result = spawnSync(empp, linkArgs, { stdio: 'inherit', env, cwd: root })
if (result.error) {
  console.error(result.error.message)
  process.exit(1)
}
process.exit(result.status === null ? 1 : result.status)
