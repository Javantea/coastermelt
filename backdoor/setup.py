#!/usr/bin/env python3

from distutils.core import setup, Extension
import sys

if sys.platform == 'darwin':
    frameworks = '-framework CoreFoundation -framework IOKit'.split()
else:
    frameworks = ''

setup(ext_modules=[

	# Main I/O module that handles our SCSI communications quickly and recklesly
	Extension('remote', ['remote.cpp'],
		extra_link_args = frameworks,
		include_dirs = ['../lib']
	),

	# Pure C++ extension module for hilbert curve math
	Extension('hilbert', ['hilbert.cpp'])
])
