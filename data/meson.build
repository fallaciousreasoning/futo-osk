# gnome-session starts the component named sm.puri.OSK0 in a phosh session.
configure_file(
  input: 'sm.puri.OSK0.desktop.in',
  output: 'sm.puri.OSK0.desktop',
  configuration: { 'bindir': get_option('prefix') / get_option('bindir') },
  install: get_option('desktop_file'),
  install_dir: get_option('datadir') / 'applications')

resources = get_option('futo_resources')
if resources != ''
  install_data(resources / 'raw' / 'main_en.dict', rename: 'main.dict', install_dir: pkgdatadir / 'en')
  install_data(resources / 'raw' / 'ml4_q6_k.gguf', rename: 'model.gguf', install_dir: pkgdatadir / 'en')
endif

# Under systemd, gnome-session ignores the desktop file and phosh's
# mobi.phosh.OSK.target starts this unit instead.
systemd_user_unit_dir = get_option('systemd_user_unit_dir')
if systemd_user_unit_dir == ''
  systemd_user_unit_dir = get_option('prefix') / 'lib' / 'systemd' / 'user'
endif
configure_file(
  input: 'mobi.phosh.OSK.service.in',
  output: 'mobi.phosh.OSK.service',
  configuration: { 'bindir': get_option('prefix') / get_option('bindir') },
  install: get_option('desktop_file'),
  install_dir: systemd_user_unit_dir)
