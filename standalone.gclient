# Copy this file to <spirv-tools clone dir>/.gclient to bootstrap gclient in a
# standalone checkout of SPIRV-Tools.

solutions = [
  { "name"        : ".",
    "url"         : "https://github.com/KhronosGroup/SPIRV-Tools",
    "deps_file"   : "gn_files/DEPS",
    "managed"     : False,
    "custom_deps": {
    },
  },
]

