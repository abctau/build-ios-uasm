{
  'targets': [
    {
      'target_name': 'UasmTuiSvgUasm',
      'type': 'shared_library',
      'sources': [
        'binding.cc',
        'svg_core.cc',
      ],
      'include_dirs': [
        '.',
      ],
      'cflags_cc': [
        '-std=c++20',
        '-fno-exceptions',
      ],
      'conditions': [
        ['OS == "android"', {
          'defines': [
            'GYP_ANDROID=1',
          ],
          'ldflags': [
            '-Wl,--unresolved-symbols=ignore-all',
            # The app supplies libc++_shared.so from its native library directory.
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
            'PRODUCT_BUNDLE_IDENTIFIER': 'uts.sdk.modules.tuiSvgUasm',
          },
          'ldflags': [
            '-Wl,-undefined,dynamic_lookup',
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
