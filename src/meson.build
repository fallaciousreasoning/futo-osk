wayland_scanner = find_program(dependency('wayland-scanner', native: true).get_variable('wayland_scanner'))

protocol_sources = []
foreach proto : ['input-method-unstable-v2', 'virtual-keyboard-unstable-v1']
  xml = meson.project_source_root() / 'protocols' / proto + '.xml'
  protocol_sources += custom_target(proto + '-client-header',
    input: xml, output: proto + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  protocol_sources += custom_target(proto + '-code',
    input: xml, output: proto + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

conf = configuration_data()
conf.set_quoted('PKGDATADIR', pkgdatadir)

gtk_dep = dependency('gtk4', version: '>= 4.12')
deps = [
  gtk_dep,
  dependency('gtk4-wayland'),
  dependency('wayland-client'),
  dependency('xkbcommon'),
  dependency('gio-2.0'),
  futo_engine_dep,
  meson.get_compiler('c').find_library('m'),
]

layer_shell = dependency('gtk4-layer-shell-0', required: get_option('layer_shell'))
if layer_shell.found()
  conf.set('HAVE_LAYER_SHELL', 1)
  # gtk4-layer-shell must be linked ahead of libwayland-client.
  deps = [layer_shell] + deps
endif
feedback = dependency('libfeedback-0.0', required: get_option('feedback'))
if feedback.found()
  conf.set('HAVE_FEEDBACK', 1)
endif
configure_file(output: 'config.h', configuration: conf)

osk_core = static_library('osk-core',
  'osk-ime.c',
  'osk-input.c',
  'osk-input-test.c',
  'osk-input-wayland.c',
  'osk-keyboard.c',
  'osk-layout.c',
  'osk-predictor.c',
  protocol_sources,
  dependencies: deps)

osk_core_dep = declare_dependency(
  link_with: osk_core,
  include_directories: include_directories('.'),
  dependencies: deps)

executable('futo-osk', 'main.c',
  dependencies: [osk_core_dep, feedback],
  link_language: 'cpp',
  install: true)
