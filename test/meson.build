# This mock library will replace libdrm
mock_drm_lib = shared_library(
	'drm',
	files('libdrm_mock.c'),
	dependencies: drm.partial_dependency(compile_args: true),
	soversion: drm.version().split('.')[0], # TODO: get it from the real dep
)

mock_liftoff = declare_dependency(
	link_with: [mock_drm_lib, liftoff_lib],
	include_directories: [liftoff_inc],
	dependencies: drm.partial_dependency(compile_args: true),
)

test('check_ndebug', executable('check_ndebug', 'check_ndebug.c'))

bench_exe = executable(
	'bench',
	files('bench.c'),
	dependencies: mock_liftoff,
)

tests = {
	'alloc': [
		'basic',
		'no-props-fail',
		'zero-fb-id-fail',
		'composition-zero-fb-id',
		'empty',
		'simple-1x',
		'simple-1x-fail',
		'simple-3x',
		'zpos-3x',
		'zpos-3x-intersect-fail',
		'zpos-3x-intersect-partial',
		'zpos-3x-disjoint-partial',
		'zpos-3x-disjoint',
		'zpos-4x-intersect-partial',
		'zpos-4x-disjoint',
		'zpos-4x-disjoint-alt',
		'zpos-4x-domino-fail',
		'zpos-4x-domino-partial',
		'composition-3x',
		'composition-3x-fail',
		'composition-3x-partial',
		'composition-3x-force',
	],
	'dynamic': [
		'same',
		'change-fb',
		'unset-fb',
		'set-fb',
		'add-layer',
		'remove-layer',
		'change-composition-layer',
		'change-alpha',
		'set-alpha-from-opaque',
		'set-alpha-from-transparent',
		'unset-alpha-to-opaque',
		'unset-alpha-to-transparent',
		'change-in-fence-fd',
		'change-fb-damage-clips',
	],
	'priority': [
		#'basic',
	],
	'prop': [
		'default-alpha',
		'default-rotation',
		'ignore-alpha',
		'immutable-zpos',
		'unmatched',
	],
}

foreach test_name, subtests : tests
	test_exe = executable(
		'test-' + test_name,
		files('test_' + test_name + '.c'),
		dependencies: mock_liftoff,
	)
	foreach subtest_name : subtests
		test(
			test_name + '@' + subtest_name,
			test_exe,
			args: [subtest_name],
		)
	endforeach
endforeach
