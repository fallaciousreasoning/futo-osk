project('futo-osk', 'c', 'cpp',
  version: '0.1.0',
  meson_version: '>= 0.63',
  default_options: ['c_std=gnu11', 'cpp_std=c++17', 'buildtype=release', 'warning_level=2'])

futo_source = get_option('futo_source')
if futo_source == ''
  futo_source = meson.project_source_root() / 'futo' / 'android-keyboard'
endif
futo_jni = futo_source / 'native' / 'jni'

add_project_arguments('-Wno-unused-parameter', language: ['c', 'cpp'])

pkgdatadir = get_option('prefix') / get_option('datadir') / meson.project_name()

subdir('engine')
subdir('src')
subdir('tests')
subdir('data')
