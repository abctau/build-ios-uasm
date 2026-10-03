'use strict'
// Merge prebuilt ffmpeg iOS static libs into the plugin xcframework slots.
// Run on macOS AFTER `uni-gyp --platform=ios` and BEFORE module-pack.
// - ios-arm64 slot              <- vendor/ffmpeg/lib/ios/iphoneos/libUasmFfmpegLibs.a
// - ios-arm64_x86_64-simulator  <- vendor/ffmpeg/lib/ios/iphonesimulator/libUasmFfmpegLibs.a
'use strict'

const fs = require('fs')
const path = require('path')
const { execFileSync } = require('child_process')

const root = path.resolve(__dirname, '..')
const xcfDir = path.join(root, 'build', 'ios', 'xcframework')
const modName = 'UasmTuiFfmpegUasm'
const xcf = path.join(pluginLibParent(), modName + '.xcframework')

// uni-gyp lays out xcframeworks under build/ios/xcframework/<Configuration>/;
// fall back to the bare dir for older layouts.
function pluginLibParent() {
  const direct = path.join(xcfDir, modName + '.xcframework')
  if (fs.existsSync(direct)) return xcfDir
  const rel = path.join(xcfDir, 'Release')
  if (fs.existsSync(path.join(rel, modName + '.xcframework'))) return rel
  return rel
}
const pluginLib = path.join(root, 'src', 'tui-ffmpeg', 'vendor', 'ffmpeg', 'lib', 'ios')

if (!fs.existsSync(xcf)) {
  console.error('[merge-ios-ffmpeg] xcframework not found: ' + xcf)
  process.exit(1)
}

function findBin(slotDir) {
  const fwDir = path.join(slotDir, modName + '.framework')
  const bin = path.join(fwDir, modName)
  if (!fs.existsSync(bin)) {
    throw new Error('framework binary not found: ' + bin)
  }
  return bin
}

function mergeSlot(slotName, libPath) {
  const bin = findBin(path.join(xcf, slotName))
  const tmp = bin + '.merged'
  console.log('[merge-ios-ffmpeg] ' + slotName + ' <- ' + libPath)
  execFileSync('xcrun', ['libtool', '-static', '-o', tmp, bin, libPath], { stdio: 'inherit' })
  // sanity check: ffmpeg symbols must be present after merge (dlopen flat namespace)
  const syms = execFileSync('xcrun', ['nm', '-gU', tmp], { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 })
  const required = ['_sws_freeContext', '_sws_scale', '_avformat_alloc_output_context2', '_avcodec_find_encoder']
  for (const s of required) {
    if (syms.indexOf(s) < 0) {
      console.error('[merge-ios-ffmpeg] missing symbol after merge: ' + s)
      process.exit(1)
    }
  }
  fs.renameSync(tmp, bin)
}

mergeSlot('ios-arm64', path.join(pluginLib, 'iphoneos', 'libUasmFfmpegLibs.a'))
mergeSlot('ios-arm64_x86_64-simulator', path.join(pluginLib, 'iphonesimulator', 'libUasmFfmpegLibs.a'))

console.log('[merge-ios-ffmpeg] merged OK')
