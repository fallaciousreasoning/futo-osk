executable('harness', 'harness.c',
  dependencies: osk_core_dep,
  link_language: 'cpp')

# Needs the dictionary and model, and gtk4-broadwayd.
if get_option('futo_resources') != '' and find_program('gtk4-broadwayd', required: false).found()
  test_data = custom_target('test-data',
    output: 'data',
    command: ['sh', '-c', 'mkdir -p @OUTPUT@/en && ln -sf "$1/raw/main_en.dict" @OUTPUT@/en/main.dict && ln -sf "$1/raw/ml4_q6_k.gguf" @OUTPUT@/en/model.gguf && gzip -dc "$2" > @OUTPUT@/en/words.combined',
              'sh', get_option('futo_resources'), futo_source / 'dictionaries' / 'en_wordlist.combined.gz'])
  test('typing', files('run-harness.sh'),
    args: [meson.project_build_root(), test_data.full_path()],
    depends: test_data,
    timeout: 120)
endif
