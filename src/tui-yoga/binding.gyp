{
  'targets': [
    {
      'target_name': 'UasmTuiYogaUasm',
      'type': 'shared_library',
      'sources': [
        'binding.cc',
        'yoga_core.cc',
        'vendor/yoga/YGConfig.cpp',
        'vendor/yoga/YGEnums.cpp',
        'vendor/yoga/YGNode.cpp',
        'vendor/yoga/YGNodeLayout.cpp',
        'vendor/yoga/YGNodeStyle.cpp',
        'vendor/yoga/YGPixelGrid.cpp',
        'vendor/yoga/YGValue.cpp',
        'vendor/yoga/algorithm/AbsoluteLayout.cpp',
        'vendor/yoga/algorithm/Baseline.cpp',
        'vendor/yoga/algorithm/Cache.cpp',
        'vendor/yoga/algorithm/CalculateLayout.cpp',
        'vendor/yoga/algorithm/FlexLine.cpp',
        'vendor/yoga/algorithm/PixelGrid.cpp',
        'vendor/yoga/config/Config.cpp',
        'vendor/yoga/debug/AssertFatal.cpp',
        'vendor/yoga/debug/Log.cpp',
        'vendor/yoga/event/event.cpp',
        'vendor/yoga/node/LayoutResults.cpp',
        'vendor/yoga/node/Node.cpp',
      ],
      'include_dirs': [
        'vendor',
      ],
      'cflags_cc': [
        '-std=c++20',
        '-fno-exceptions',
      ],
      'defines': [
        'NAPI_DISABLE_CPP_EXCEPTIONS=1',
      ],
      'conditions': [
        ['OS == "android"', {
          'defines': [
            'GYP_ANDROID=1',
          ],
          'cflags_cc': [
            '-O2',
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
            'CLANG_CXX_LANGUAGE_STANDARD': 'c++20',
            'GCC_PREPROCESSOR_DEFINITIONS': [
              'NAPI_DISABLE_CPP_EXCEPTIONS=1',
            ],
            'PRODUCT_BUNDLE_IDENTIFIER': 'uts.sdk.modules.tuiYogaUasm',
          },
          'ldflags': [
            '-Wl,-undefined,dynamic_lookup',
          ],
        }],
        ['OS == "harmony"', {
          'defines': [
            'GYP_HARMONY=1',
          ],
          'cflags_cc': [
            '-O2',
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
