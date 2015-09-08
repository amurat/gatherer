#!/usr/bin/env python3

# Copyright (c) 2014, Ruslan Baratov
# All rights reserved.

# https://github.com/ruslo/polly/wiki/Jenkins

import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import argparse

def run():
  parser = argparse.ArgumentParser("Testing script")
  parser.add_argument(
      '--nocreate',
      action='store_true',
      help='Do not create Hunter archive (reusing old)'
  )
  parser.add_argument(
      '--clear',
      action='store_true',
      help='Remove old testing directories'
  )
  parser.add_argument(
      '--clear-except-download',
      action='store_true',
      help='Remove old testing directories except `Download` directory'
  )
  parser.add_argument(
      '--verbose',
      action='store_true',
      help='Verbose output'
  )

  parsed_args = parser.parse_args()

  cdir = os.getcwd()
  hunter_root = cdir

  toolchain = os.getenv('TOOLCHAIN')
  if not toolchain:
    sys.exit('Environment variable TOOLCHAIN is empty')

  project_dir = os.getenv('PROJECT_DIR')
  if not project_dir:
    sys.exit('Expected environment variable PROJECT_DIR')

  # Check broken builds --
  # TODO: ...
  # -- end

  verbose = True
  project_dir = os.path.join(cdir, project_dir)
  project_dir = os.path.normpath(project_dir)

  testing_dir = os.path.join(os.getcwd(), '_testing')
  if os.path.exists(testing_dir) and parsed_args.clear:
    print('REMOVING: {}'.format(testing_dir))
    shutil.rmtree(testing_dir)
  os.makedirs(testing_dir, exist_ok=True)

  if os.name == 'nt':
    hunter_junctions = os.getenv('HUNTER_JUNCTIONS')
    if hunter_junctions:
      temp_dir = tempfile.mkdtemp(dir=hunter_junctions)
      shutil.rmtree(temp_dir)
      subprocess.check_output(
          "cmd /c mklink /J {} {}".format(temp_dir, testing_dir)
      )
      testing_dir = temp_dir

  hunter_url = os.path.join(testing_dir, 'hunter.tar.gz')

  if parsed_args.nocreate:
    if not os.path.exists(hunter_url):
      sys.exit('Option `--nocreate` but no archive')
  else:
    arch = tarfile.open(hunter_url, 'w:gz')
    arch.add('cmake')
    arch.add('scripts')
    arch.close()

  hunter_sha1 = hashlib.sha1(open(hunter_url, 'rb').read()).hexdigest()

  hunter_root = os.path.join(testing_dir, 'Hunter')

  if parsed_args.clear_except_download:
    base_dir = os.path.join(hunter_root, '_Base')
    if os.path.exists(base_dir):
      print('Clearing directory: {}'.format(base_dir))
      for filename in os.listdir(base_dir):
        if filename != 'Download':
          to_remove = os.path.join(base_dir, filename)
          if os.name == 'nt':
            # Fix "path too long" error
            subprocess.check_call(['cmd', '/c', 'rmdir', to_remove, '/S', '/Q'])
          else:
            shutil.rmtree(to_remove)

  build_script = 'build.py'
  if os.name == 'nt':
    which = 'where'
  else:
    which = 'which'

  build_script = subprocess.check_output(
      [which, 'build.py'], universal_newlines=True
  ).split('\n')[0]

  print('Testing in: {}'.format(testing_dir))
  os.chdir(testing_dir)

  if not parsed_args.verbose:
    args += ['HUNTER_STATUS_DEBUG=NO']

  if not parsed_args.nocreate:
    args += ['HUNTER_RUN_INSTALL=ON']

  if verbose:
    args += ['--verbose']

  print('Execute command: [')
  for i in args:
    print('  `{}`'.format(i))
  print(']')

  subprocess.check_call(args)

if __name__ == "__main__":
  run()
