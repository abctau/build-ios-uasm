{
  'variables': {
    'module-root-dir': '<!(node -p "require(\'path\').resolve(\'.\')")',
    'common-sources': [
      'binding.cc',
    ],
  },
  'targets': [
    {
      'target_name': 'UasmTestUasm',
      'type': 'shared_library',
      'sources': [
        '<@(common-sources)',
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
          'sources': [
            'jni_binding.cc',
          ],
          'ldflags': [
            '-Wl,--unresolved-symbols=ignore-all',
            '-Wl,--version-script=<(module-root-dir)/android.exports',
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
          'sources': [
            'swift_binding.cc',
            'swift_binding.h',
          ],
          'mac_framework_headers': [
            'swift_binding.h',
          ],
          'xcode_settings': {
            'PRODUCT_BUNDLE_IDENTIFIER': 'uts.sdk.modules.testUasm',
          },
          'ldflags': [
            '-Wl,-undefined,dynamic_lookup',
            '-Wl,-exported_symbols_list,<(module-root-dir)/ios.exports',
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
