'use strict'

// Build the tui-zip WASM artifact (same flow as build-wasm-tui-color-thief.js,
// targeting src/tui-zip/wasm_binding.cc + zip_core.cc + vendor/zip.c).
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
  console.error('[build-wasm-tui-zip] em++ not found: ' + empp)
  console.error('[build-wasm-tui-zip] set EMSDK to your emsdk install dir (default: E:/emsdk)')
  process.exit(1)
}

const outDir = path.join(root, 'web', 'release')
fs.mkdirSync(outDir, { recursive: true })
// The web entry must be named after the uni_modules plugin directory:
// uni_modules/tui-zip-uasm/uasm/web/tui-zip-uasm.js
const outJs = path.join(outDir, 'tui-zip-uasm.js')

// Compile in two steps: C++ sources need -std=c++20, while the vendored
// zip.c must stay in C mode (-x c) and needs the configure-detected macro
// ZIP_HAVE_SYMLINK to pull in <unistd.h>.
const commonIncludes = ['-I', path.join(root, 'src', 'tui-zip')]
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
  '-s', 'EXPORT_NAME=createTuiZipUasmModule',
]

const steps = [
  {
    label: 'wasm_binding.cc',
    args: [path.join(root, 'src', 'tui-zip', 'wasm_binding.cc'), ...commonIncludes, '-std=c++20', '-c'],
    out: 'tui-zip-binding.o',
  },
  {
    label: 'zip_core.cc',
    args: [path.join(root, 'src', 'tui-zip', 'zip_core.cc'), ...commonIncludes, '-std=c++20', '-c'],
    out: 'tui-zip-core.o',
  },
  {
    label: 'vendor zip.c',
    args: [path.join(root, 'src', 'tui-zip', 'vendor', 'zip.c'), ...commonIncludes, '-x', 'c', '-DZIP_HAVE_SYMLINK=1', '-c'],
    out: 'tui-zip-vendor.o',
  },
]

function run(emppArgs, label) {
  console.log('[build-wasm-tui-zip:' + label + '] ' + emppArgs.join(' '))
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

const objs = steps.map((step) => {
  const outPath = path.join(outDir, step.out)
  run([...step.args, '-o', outPath], step.label)
  return outPath
})

const linkArgs = [...objs, '-o', outJs, ...commonFlags]
console.log('[build-wasm-tui-zip:link] ' + empp + ' ' + linkArgs.join(' '))
const result = spawnSync(empp, linkArgs, { stdio: 'inherit', env, cwd: root })
if (result.error) {
  console.error(result.error.message)
  process.exit(1)
}
process.exit(result.status === null ? 1 : result.status)
