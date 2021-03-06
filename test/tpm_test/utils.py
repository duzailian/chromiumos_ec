# -*- coding: utf-8 -*-
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Support functions for extended command based testing."""

import json
import sys

if hasattr(sys.stdout, 'isatty') and sys.stdout.isatty():
    CURSOR_BACK_CMD = '\x1b[1D'  # Move one space to the left.
else:
    CURSOR_BACK_CMD = ''


def cursor_back():
    """Return a string which would move cursor one space left, if available.

    This is used to remove the remaining 'spinner' character after the test
    completes and its result is printed on the same line where the 'spinner' was
    spinning.
    """
    return CURSOR_BACK_CMD


def hex_dump(binstr):
    """Convert binary string into its multiline hex representation."""

    dump_lines = ['',]
    i = 0
    while i < len(binstr):
        strsize = min(16, len(binstr) - i)
        hexstr = ' '.join('%2.2x' % x for x in binstr[i:i+strsize])
        dump_lines.append(hexstr)
        i += strsize
    dump_lines.append('')
    return '\n'.join(dump_lines)

def write_test_result_json(filename, results):
    """Write the test results to the given file."""
    with open(filename, 'w') as json_file:
        json.dump(results, json_file, indent=4)

def read_vectors(filename):
    """Read the test vectors from the given json file."""
    with open(filename, 'r') as json_file:
        contents = json.load(json_file)
    return contents
