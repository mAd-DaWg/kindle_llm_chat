project('kindle_llm_chat', 'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
  ]
)

gtk_dep = dependency('gtk+-2.0')
glib_dep = dependency('glib-2.0')
json_glib_dep = dependency('json-glib-1.0')
curl_dep = dependency('libcurl')

inc = include_directories('src')

sources = files(
  'src/main.cpp',
  'src/app_window.cpp',
  'src/chat_store.cpp',
  'src/config.cpp',
  'src/http_client.cpp',
  'src/stream_parser.cpp',
  'src/osk.cpp',
)

executable(
  'kindle-llm-chat',
  sources,
  include_directories: inc,
  dependencies: [gtk_dep, glib_dep, json_glib_dep, curl_dep],
  cpp_args: '-static-libstdc++', 
  link_args: '-static-libstdc++'
)
