project(
	'hwc',
	'c',
	version: '0.0.0',
	license: 'MIT',
	meson_version: '>=0.47.0',
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

hwc_inc = include_directories('include')

drm = dependency('libdrm')

hwc_deps = [drm]

hwc_lib = library(
	meson.project_name(),
	files(
		'display.c',
		'layer.c',
		'list.c',
		'output.c',
	),
	include_directories: hwc_inc,
	version: meson.project_version(),
	dependencies: hwc_deps,
	install: true,
)

hwc = declare_dependency(
	link_with: hwc_lib,
	include_directories: hwc_inc,
	dependencies: hwc_deps,
)

executable(
	'example',
	files('example.c'),
	dependencies: [hwc],
)

pkgconfig = import('pkgconfig')
pkgconfig.generate(
	libraries: hwc_lib,
	version: meson.project_version(),
	filebase: meson.project_name(),
	name: meson.project_name(),
	description: 'Hardware composer library',
)
