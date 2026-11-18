futo_files = []
foreach f : run_command(meson.project_source_root() / 'scripts' / 'futo-sources.sh',
                        futo_source, check: true).stdout().strip().split('\n')
  futo_files += futo_jni / f
endforeach

# FUTO's tree may sit inside ours (subprojects/), where meson refuses
# absolute include_directories, so pass its include paths as flags.
futo_inc_args = []
foreach d : ['', 'src', 'src/sentencepiece/builtin_pb', 'src/third_party',
             'src/third_party/protobuf-lite', 'src/third_party/esaxx',
             'src/third_party/darts_clone', 'src/third_party/absl']
  futo_inc_args += '-I' + (futo_jni / d)
endforeach
futo_inc = include_directories('.') # our jni.h

# FUTO's code isn't ours to tidy, so its warnings are silenced. GGML is
# unusably slow without optimisation, so always optimise.
futo_args = ['-w', '-O3', '-DHAVE_PTHREAD', '-fexceptions'] + futo_inc_args

futo_core = static_library('futo-core', futo_files,
  include_directories: futo_inc,
  c_args: futo_args,
  cpp_args: futo_args,
  override_options: ['warning_level=0'],
  dependencies: dependency('threads'))

# These include FUTO's JNI glue, hence its flags.
futo_glue = static_library('futo-glue',
  'glue-dictionary.cpp', 'glue-lm.cpp', 'glue-proximity.cpp', 'glue-session.cpp',
  include_directories: futo_inc,
  cpp_args: futo_args,
  override_options: ['warning_level=0'])

futo_engine = static_library('futo-engine',
  'fake-jni.cpp',
  'futo-engine.cpp',
  include_directories: futo_inc,
  cpp_args: futo_inc_args,
  link_with: [futo_glue, futo_core])

futo_engine_dep = declare_dependency(
  link_with: futo_engine,
  include_directories: include_directories('.'),
  dependencies: dependency('threads'))

executable('futo-engine-cli', 'engine-cli.c',
  dependencies: futo_engine_dep,
  link_language: 'cpp',
  install: false)
