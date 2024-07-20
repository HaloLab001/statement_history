# Copyright (c) 2019-2024, Halo Tech Co.,Ltd. All rights reserved.

pg_stat_statements_sources = files(
  'statement_history.c',
)

if host_system == 'windows'
  pg_stat_statements_sources += rc_lib_gen.process(win32ver_rc, extra_args: [
    '--NAME', 'statement_history',
    '--FILEDESC', 'statement_history - track planning and execution statistics and other performance related info of all SQL statements executed',])
endif

statement_history = shared_module('statement_history',
  pg_stat_statements_sources,
  kwargs: contrib_mod_args + {
    'dependencies': contrib_mod_args['dependencies'],
  },
)
contrib_targets += statement_history

install_data(
  'statement_history.control',
  'statement_history--1.0.sql',
  kwargs: contrib_data_args,
)

tests += {
  'name': 'statement_history',
  'sd': meson.current_source_dir(),
  'bd': meson.current_build_dir(),
  'regress': {
    'sql': [
      'select',
      'dml',
      'cursors',
      'utility',
      'level_tracking',
      'planning',
      'user_activity',
      'wal',
      'cleanup',
      'oldextversions',
    ],
    'regress_args': ['--temp-config', files('statement_history.conf')],
    # Disabled because these tests require
    # "shared_preload_libraries=statement_history", which typical
    # runningcheck users do not have (e.g. buildfarm clients).
    'runningcheck': false,
  },
}
