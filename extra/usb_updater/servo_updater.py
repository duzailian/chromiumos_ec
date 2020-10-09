#!/usr/bin/env python
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Note: This is a py2/3 compatible file.

from __future__ import print_function

import argparse
import errno
import os
import re
import subprocess
import time
import tempfile

import json

import fw_update
import ecusb.tiny_servo_common as c


class ServoUpdaterException(Exception):
  """Raised on exceptions generated by servo_updater."""

BOARD_C2D2 = 'c2d2'
BOARD_SERVO_MICRO = 'servo_micro'
BOARD_SERVO_V4 = 'servo_v4'
BOARD_SERVO_V4P1 = 'servo_v4p1'
BOARD_SWEETBERRY = 'sweetberry'

DEFAULT_BOARD = BOARD_SERVO_V4

DEFAULT_BASE_PATH = '/usr/'
TEST_IMAGE_BASE_PATH = '/usr/local/'

COMMON_PATH = 'share/servo_updater'

FIRMWARE_DIR = "firmware/"
CONFIGS_DIR = "configs/"

def flash(brdfile, serialno, binfile):
  """Call fw_update to upload to updater USB endpoint."""
  p = fw_update.Supdate()
  p.load_board(brdfile)
  p.connect_usb(serialname=serialno)
  p.load_file(binfile)

  # Start transfer and erase.
  p.start()
  # Upload the bin file
  print("Uploading %s" % binfile)
  p.write_file()

  # Finalize
  print("Done. Finalizing.")
  p.stop()

def flash2(vidpid, serialno, binfile):
  """Call fw update via usb_updater2 commandline."""
  tool = 'usb_updater2'
  cmd = "%s -d %s" % (tool, vidpid)
  if serialno:
    cmd += " -S %s" % serialno
  cmd += " -n"
  cmd += " %s" % binfile

  print(cmd)
  help_cmd = '%s --help' % tool
  with open('/dev/null') as devnull:
    valid_check = subprocess.call(help_cmd.split(), stdout=devnull,
                                  stderr=devnull)
  if valid_check:
    raise ServoUpdaterException('%s exit with res = %d. Make sure the tool '
                                'is available on the device.' % (help_cmd,
                                                                 valid_check))
  res = subprocess.call(cmd.split())

  if res in (0, 1, 2):
    return res
  else:
    raise ServoUpdaterException("%s exit with res = %d" % (cmd, res))

def connect(vidpid, iface, serialno, debuglog=False):
  """Connect to console.

  Args:
    vidpid: vidpid of desired device.
    iface: interface to connect.
    serialno: serial number, to differentiate multiple devices.
    debuglog: do chatty log.

  Returns:
    a connected pty object.
  """
  # Make sure device is up.
  c.wait_for_usb(vidpid, serialname=serialno)

  # make a console.
  pty = c.setup_tinyservod(vidpid, iface,
            serialname=serialno, debuglog=debuglog)

  return pty

def select(vidpid, iface, serialno, region, debuglog=False):
  """Ensure the servo is in the expected ro/rw partition."""

  if region not in ["rw", "ro"]:
    raise Exception("Region must be ro or rw")

  pty = connect(vidpid, iface, serialno)

  if region is "ro":
    cmd = "reboot"
  else:
    cmd = "sysjump %s" % region
  pty._issue_cmd(cmd)
  time.sleep(1)
  pty.close()

def do_version(vidpid, iface, serialno):
  """Check version via ec console 'pty'.

  Args:
    see connect()

  Returns:
    detected version number

  Commands are:
  # > version
  # ...
  # Build:   tigertail_v1.1.6749-74d1a312e
  """
  pty = connect(vidpid, iface, serialno)

  cmd = '\r\nversion\r\n'
  regex = 'Build:\s+(\S+)[\r\n]+'

  results = pty._issue_cmd_get_results(cmd, [regex])[0]
  pty.close()

  return results[1].strip(' \t\r\n\0')

def do_updater_version(vidpid, iface, serialno):
  """Check whether this uses python updater or c++ updater

  Args:
    see connect()

  Returns:
    updater version number. 2 or 6.
  """
  vers = do_version(vidpid, iface, serialno)

  # Servo versions below 58 are from servo-9040.B. Versions starting with _v2
  # are newer than anything _v1, no need to check the exact number. Updater
  # version is not directly queryable.
  if re.search('_v[2-9]\.\d', vers):
    return 6
  m = re.search('_v1\.1\.(\d\d\d\d)', vers)
  if m:
    version_number = int(m.group(1))
    if version_number < 5800:
      return 2
    else:
      return 6
  raise ServoUpdaterException(
      "Can't determine updater target from vers: [%s]" % vers)

def findfiles(cname, fname):
  """Select config and firmware binary files.

  This checks default file names and paths.
  In: /usr/share/servo_updater/[firmware|configs]
  check for board.json, board.bin

  Args:
    cname: board name, or config name. eg. "servo_v4" or "servo_v4.json"
    fname: firmware binary name. Can be None to try default.
  Returns:
    cname, fname: validated filenames selected from the path.
  """
  for p in (DEFAULT_BASE_PATH, TEST_IMAGE_BASE_PATH):
    updater_path = os.path.join(p, COMMON_PATH)
    if os.path.exists(updater_path):
      break
  else:
    raise ServoUpdaterException('servo_updater/ dir not found in known spots.')

  firmware_path = os.path.join(updater_path, FIRMWARE_DIR)
  configs_path = os.path.join(updater_path, CONFIGS_DIR)

  for p in (firmware_path, configs_path):
    if not os.path.exists(p):
      raise ServoUpdaterException('Could not find required path %r' % p)

  if not os.path.isfile(cname):
    # If not an existing file, try checking on the default path.
    newname = os.path.join(configs_path, cname)
    if os.path.isfile(newname):
      cname = newname
    else:
      # Try appending ".json" to convert board name to config file.
      cname = newname + ".json"
    if not os.path.isfile(cname):
      raise ServoUpdaterException("Can't find config file: %s." % cname)

  if not fname:
    # If None, try to infer board name from config.
    with open(cname) as data_file:
      data = json.load(data_file)
    boardname = data['board']

    binary_file = boardname + ".bin"
    newname = os.path.join(firmware_path, binary_file)
    if os.path.isfile(newname):
      fname = newname
    else:
      raise ServoUpdaterException("Can't find firmware binary: %s." % binary_file)
  elif not os.path.isfile(fname):
    # If a name is specified but not found, try the default path.
    newname = os.path.join(firmware_path, fname)
    if os.path.isfile(newname):
      fname = newname
    else:
      raise ServoUpdaterException("Can't find file: %s." % fname)

  return cname, fname

def find_available_version(boardname, binfile):
  """Find the version string from the binary file.

  Args:
    boardname: the name of the board, eg. "servo_micro"
    binfile: the binary to search

  Returns:
    the version string.
  """
  rawstrings = subprocess.check_output(
      ['cbfstool', binfile, 'read', '-r', 'RO_FRID', '-f', '/dev/stdout'],
      **c.get_subprocess_args())
  m = re.match(r'%s_v\S+' % boardname, rawstrings)
  if m:
    newvers = m.group(0).strip(' \t\r\n\0')
  else:
    raise ServoUpdaterException("Can't find version from file: %s." % binfile)

  return newvers

def main():
  parser = argparse.ArgumentParser(description="Image a servo micro device")
  parser.add_argument('-s', '--serialno', type=str,
      help="serial number to program", default=None)
  parser.add_argument('-b', '--board', type=str,
      help="Board configuration json file", default=DEFAULT_BOARD)
  parser.add_argument('-f', '--file', type=str,
      help="Complete ec.bin file", default=None)
  parser.add_argument('--force', action="store_true",
      help="Update even if version match", default=False)
  parser.add_argument('-v', '--verbose', action="store_true",
      help="Chatty output")
  parser.add_argument('-r', '--reboot', action="store_true",
      help="Always reboot, even after probe.")

  args = parser.parse_args()

  brdfile, binfile = findfiles(args.board, args.file)

  serialno = args.serialno
  debuglog = (args.verbose is True)

  with open(brdfile) as data_file:
      data = json.load(data_file)

  vidpid = "%04x:%04x" % (int(data['vid'], 0), int(data['pid'], 0))
  iface = int(data['console'], 0)
  boardname = data['board']

  if not args.force:
    vers = do_version(vidpid, iface, serialno)
    print("Current %s version is   %s" % (boardname, vers))

    newvers = find_available_version(boardname, binfile)
    print("Available %s version is %s" % (boardname, newvers))

    if newvers == vers:
      print("No version update needed")
      if args.reboot is True:
        select(vidpid, iface, serialno, "ro", debuglog=debuglog)
      return
    else:
      print("Updating to recommended version.")


  select(vidpid, iface, serialno, "ro", debuglog=debuglog)

  vers = do_updater_version(vidpid, iface, serialno)
  if vers == 2:
    flash(brdfile, serialno, binfile)
  elif vers == 6:
    flash2(vidpid, serialno, binfile)
  else:
    raise ServoUpdaterException("Can't detect updater version")

  select(vidpid, iface, serialno, "rw", debuglog=debuglog)

  vers = do_updater_version(vidpid, iface, serialno)
  if vers == 2:
    flash(brdfile, serialno, binfile)
  elif vers == 6:
    flash2(vidpid, serialno, binfile)
  else:
    raise ServoUpdaterException("Can't detect updater version")

  select(vidpid, iface, serialno, "ro", debuglog=debuglog)

if __name__ == "__main__":
  main()
