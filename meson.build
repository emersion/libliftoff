project(
	'liftoff',
	'c',
	version: '0.0.0',
	license: 'MIT',
	meson_version: '>=0.52.0',
	default_options: [
		'c_std=c99',
		'warning_level=3',
		'werror=true',
	],
)

cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments([
	'-Wno-missing-braces',
	'-Wno-unused-parameter',
]), language: 'c')

liftoff_inc = include_directories('include')

drm = dependency('libdrm', include_type: 'system')

liftoff_deps = [drm]

liftoff_lib = library(
	meson.project_name(),
	files(
		'alloc.c',
		'device.c',
		'layer.c',
		'list.c',
		'log.c',
		'output.c',
		'plane.c',
	),
	include_directories: liftoff_inc,
	version: meson.project_version(),
	dependencies: liftoff_deps,
	install: true,
)

liftoff = declare_dependency(
	link_with: liftoff_lib,
	include_directories: liftoff_inc,
	dependencies: liftoff_deps,
)

install_headers('include/libliftoff.h')

pkgconfig = import('pkgconfig')
pkgconfig.generate(
	libraries: liftoff_lib,
	version: meson.project_version(),
	filebase: meson.project_name(),
	name: meson.project_name(),
	description: 'Hardware composer library',
)

subdir('example')
subdir('test')
