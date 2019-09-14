test_inc = include_directories('include')

# This mock library will replace libdrm
mock_drm_lib = shared_library(
	'drm',
	files('libdrm_mock.c'),
	include_directories: [test_inc],
	dependencies: drm.partial_dependency(compile_args: true),
	soversion: drm.version().split('.')[0], # TODO: get it from the real dep
)

mock_liftoff = declare_dependency(
	link_with: [mock_drm_lib, liftoff_lib],
	include_directories: [liftoff_inc, test_inc],
	dependencies: drm.partial_dependency(compile_args: true),
)

test_alloc_exe = executable(
	'test-alloc',
	files('test_alloc.c'),
	dependencies: mock_liftoff,
)

alloc_tests = [
	'basic',
	'simple-1x',
	'simple-1x-fail',
	'simple-3x',
	'zpos-3x',
	#'zpos-3x-intersect-fail',
	'zpos-3x-intersect-partial',
	#'zpos-3x-disjoint-partial',
	#'zpos-3x-disjoint',
	'zpos-4x-intersect-partial',
	#'zpos-4x-disjoint',
]

foreach name : alloc_tests
	test(
		'alloc@' + name,
		test_alloc_exe,
		args: [name],
	)
endforeach
