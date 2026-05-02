project('kindle_llm_chat', ['c', 'cpp'],
  version: '0.1.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
  ]
)

gtk_dep = dependency('gtk+-2.0')
glib_dep = dependency('glib-2.0')
curl_dep = dependency('libcurl')

inc = include_directories('src')
md4c_inc = include_directories('third_party/md4c/src')
cjson_inc = include_directories('third_party/cjson')

cjson_lib = static_library(
  'cjson',
  'third_party/cjson/cJSON.c',
  include_directories: cjson_inc,
  override_options: ['c_std=c99'],
)

cjson_dep = declare_dependency(
  link_with: cjson_lib,
  include_directories: cjson_inc,
)

md4c_lib = static_library(
  'md4c',
  'third_party/md4c/src/md4c.c',
  include_directories: md4c_inc,
  override_options: ['c_std=c99'],
)

md4c_dep = declare_dependency(
  link_with: md4c_lib,
  include_directories: md4c_inc,
)

sources = files(
  'src/main.cpp',
  'src/app_window.cpp',
  'src/chat_store.cpp',
  'src/config.cpp',
  'src/http_client.cpp',
  'src/stream_parser.cpp',
  'src/osk.cpp',
  'src/markdown_view.cpp',
)

executable(
  'kindle-llm-chat',
  sources,
  include_directories: inc,
  dependencies: [gtk_dep, glib_dep, cjson_dep, curl_dep, md4c_dep],
  cpp_args: '-static-libstdc++',
  link_args: '-static-libstdc++'
)
