#!/usr/bin/env python3

from distutils.core import setup, Extension

frameworks = '-framework CoreFoundation -framework IOKit'.split()

setup(ext_modules=[

	# Main I/O module that handles our SCSI communications quickly and recklesly
	Extension('remote', ['remote.cpp'],
		extra_link_args = frameworks,
		include_dirs = ['../lib']
	),

	# Pure C++ extension module for hilbert curve math
	Extension('hilbert', ['hilbert.cpp'])
])