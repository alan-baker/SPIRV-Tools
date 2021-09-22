# Copy this file to <spirv-tools clone dir>/.gclient to bootstrap gclient in a
# standalone checkout of SPIRV-Tools.

solutions = [
  { "name"        : ".",
    "url"         : "https://github.com/KhronosGroup/SPIRV-Tools",
    "deps_file"   : "DEPS",
    "managed"     : False,
    "custom_deps": {
      './build': 'https://chromium.googlesource.com/chromium/src/build.git@85ee3b7692e5284f08bd3c9459fb5685eed7b838',
      './buildtools': 'https://chromium.googlesource.com/chromium/src/buildtools.git@4be464e050b3d05060471788f926b34c641db9fd',
      './tools/clang': 'https://chromium.googlesource.com/chromium/src/tools/clang.git@3a982adabb720aa8f3e3885d40bf3fe506990157',
    },
  },
]

