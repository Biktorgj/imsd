# Copyright (C) 2024 Biktorgj <biktor@biktorgj.com>
# Copyright (C) 2024 Lynxis [....]
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
project (
  'imsd',
  'c',
  version : '0.0.5',
  license : 'LGPLv3+',
  meson_version : '>= 0.50.0',
  default_options :
    [
      'warning_level=1',
      'buildtype=debugoptimized',
      'c_std=gnu11'
    ],
)

glib_version = '2.56'

glib_dep = dependency('glib-2.0', version: '>= ' + glib_version)
gio_unix_dep = dependency('gio-unix-2.0')

libqrtr = meson.get_compiler('c').find_library('qrtr')

c_flags = [
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_' + glib_version.underscorify(),
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_' + glib_version.underscorify(),
  '-DGLIB_DISABLE_DEPRECATION_WARNINGS',
]

deps = [
  glib_dep,
  dependency('gio-2.0'),
  dependency('gobject-2.0'),
  dependency('qmi-glib'),
  dependency('qrtr-glib'),
  dependency('inih'),
  libqrtr,
]

glib_deps = declare_dependency(
  dependencies: deps,
  compile_args: c_flags,
)

executable('imsd',
  'imsd.c',
  'wds.c',
  'nas.c',
  'imsa.c',
  'imss.c',
  'dms.c',
  'dcm.c',
  'pdc.c',
  'pdc_locate.c',
  'qmi-util.c',
  'qmi-ims-client.c',
  'config.c',
  dependencies: deps,
)
