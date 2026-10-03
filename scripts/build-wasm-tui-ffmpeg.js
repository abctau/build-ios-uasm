'use strict'

// Build the tui-ffmpeg WASM artifact: C++ binding + core linked against the
// prebuilt emscripten ffmpeg static libs (build-emscripten).
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
  console.error('[build-wasm-tui-ffmpeg] em++ not found: ' + empp)
  console.error('[build-wasm-tui-ffmpeg] set EMSDK to your emsdk install dir (default: E:/emsdk)')
  process.exit(1)
}

const outDir = path.join(root, 'web', 'release')
fs.mkdirSync(outDir, { recursive: true })
// The web entry must be named after the uni_modules plugin directory:
// uni_modules/tui-ffmpeg-uasm/uasm/web/tui-ffmpeg-uasm.js
const outJs = path.join(outDir, 'tui-ffmpeg-uasm.js')

const ffmpegLib = path.join(root, 'src', 'tui-ffmpeg', 'vendor', 'ffmpeg', 'lib', 'wasm')
for (const name of ['libavformat.a', 'libavcodec.a', 'libswscale.a', 'libavutil.a']) {
  if (!fs.existsSync(path.join(ffmpegLib, name))) {
    console.error('[build-wasm-tui-ffmpeg] missing ' + name + ' in ' + ffmpegLib)
    console.error('[build-wasm-tui-ffmpeg] copy from E:/ffmpeg-spike/ffmpeg-7.1.1/build-emscripten/lib*/')
    process.exit(1)
  }
}

const commonIncludes = ['-I', path.join(root, 'src', 'tui-ffmpeg'), '-I', path.join(root, 'src', 'tui-ffmpeg', 'vendor', 'ffmpeg', 'include')]
const commonFlags = [
  '--no-entry',
  '-lembind',
  '-s', 'WASM=1',
  '-s', 'ALLOW_MEMORY_GROWTH=1',
  '-s', 'INITIAL_MEMORY=64MB',
  '-s', 'STACK_SIZE=2MB',
  '-s', 'EXPORTED_FUNCTIONS=["_malloc","_free"]',
  '-s', 'DYNAMIC_EXECUTION=0',
  '-O2',
  '-s', 'MODULARIZE=1',
  '-s', 'EXPORT_ES6=1',
  '-s', 'ENVIRONMENT=web',
  '-s', 'EXPORT_NAME=createTuiFfmpegUasmModule',
]

const steps = [
  {
    label: 'wasm_binding.cc',
    args: [path.join(root, 'src', 'tui-ffmpeg', 'wasm_binding.cc'), ...commonIncludes, '-std=c++20', '-fno-exceptions', '-c'],
    out: 'tui-ffmpeg-binding.o',
  },
  {
    label: 'ffmpeg_core.cc',
    args: [path.join(root, 'src', 'tui-ffmpeg', 'ffmpeg_core.cc'), ...commonIncludes, '-std=c++20', '-fno-exceptions', '-c'],
    out: 'tui-ffmpeg-core.o',
  },
]

function run(emppArgs, label) {
  console.log('[build-wasm-tui-ffmpeg:' + label + '] ' + emppArgs.join(' '))
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

// static libs must come after objects (single pass ld)
const linkArgs = [
  ...objs,
  path.join(ffmpegLib, 'libavformat.a'),
  path.join(ffmpegLib, 'libavcodec.a'),
  path.join(ffmpegLib, 'libswscale.a'),
  path.join(ffmpegLib, 'libavutil.a'),
  '-o', outJs,
  ...commonFlags,
]
console.log('[build-wasm-tui-ffmpeg:link] ' + linkArgs.join(' '))
const result = spawnSync(empp, linkArgs, { stdio: 'inherit', env, cwd: root })
if (result.error) {
  console.error(result.error.message)
  process.exit(1)
}
process.exit(result.status === null ? 1 : result.status)
