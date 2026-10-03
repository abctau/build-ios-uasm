{
  'targets': [
    {
      'target_name': 'UasmTuiFfmpegUasm',
      'type': 'shared_library',
      'sources': [
        'binding.cc',
        'ffmpeg_core.cc',
        'transcode_core.cc',
      ],
      'include_dirs': [
        '.',
        'vendor/ffmpeg/include',
      ],
      'cflags_cc': [
        '-std=c++20',
        '-fno-exceptions',
      ],
      'conditions': [
        ['OS == "android" and target_arch=="arm64"', {
          'ldflags': [
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/arm64-v8a/libavfilter.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/arm64-v8a/libswresample.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/arm64-v8a/libavformat.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/arm64-v8a/libavcodec.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/arm64-v8a/libswscale.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/arm64-v8a/libavutil.a',
          ],
        }],
        ['OS == "android" and (target_arch=="x64" or target_arch=="x86_64")', {
          'ldflags': [
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/x86_64/libavfilter.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/x86_64/libswresample.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/x86_64/libavformat.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/x86_64/libavcodec.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/x86_64/libswscale.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/x86_64/libavutil.a',
          ],
        }],
        ['OS == "harmony" and target_arch=="arm64"', {
          'ldflags': [
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/ohos-arm64/libavfilter.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/ohos-arm64/libswresample.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/ohos-arm64/libavformat.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/ohos-arm64/libavcodec.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/ohos-arm64/libswscale.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/ohos-arm64/libavutil.a',
          ],
        }],
        ['OS == "harmony" and (target_arch=="x64" or target_arch=="x86_64")', {
          'ldflags': [
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/ohos-x86_64/libavfilter.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/ohos-x86_64/libswresample.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/ohos-x86_64/libavformat.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/ohos-x86_64/libavcodec.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/ohos-x86_64/libswscale.a',
            'E:/uvue/uasm-test/src/tui-ffmpeg/vendor/ffmpeg/lib/ohos-x86_64/libavutil.a',
          ],
        }],
        ['OS == "android"', {
          'defines': [
            'GYP_ANDROID=1',
          ],
          'ldflags': [
            '-Wl,--unresolved-symbols=ignore-all',
            '-nostdlib++',
          ],
          'libraries': [
            '-lc++_shared',
          ],
        }],
        ['OS == "ios"', {
          'mac_bundle': 1,
          'defines': [
            'GYP_IOS=1',
          ],
          'xcode_settings': {
            'PRODUCT_BUNDLE_IDENTIFIER': 'uts.sdk.modules.tuiFfmpegUasm',
          },
          'ldflags': [
            '-Wl,-undefined,dynamic_lookup',
            '-Wl,-dead_strip',
            '<!(node -p "process.cwd()")/src/tui-ffmpeg/vendor/ffmpeg/lib/ios/libUasmFfmpegLibs.a',
          ],
        }],
        ['OS == "harmony"', {
          'defines': [
            'GYP_HARMONY=1',
          ],
          'libraries': [
            '-lace_napi.z',
          ],
          'ldflags': [
            '-Wl,--unresolved-symbols=ignore-all',
          ],
        }],
        ['OS != "win"', {
          'cflags': [
            '-fvisibility=hidden',
          ],
        }],
      ],
    },
  ],
}
