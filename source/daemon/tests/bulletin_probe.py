#!/usr/bin/env python3
"""Build the isolated strappyd/SpringBoard bulletin experiment; never deploy.

All compilation, signing, packaging and analysis use the production Make entry
point. The sender compiles the real strappyd with an opt-in, pre-bootstrap probe.
The executable product's -dynamiclib flag builds the SpringBoard component;
INSTALL_PATH places that signed helper under MobileSubstrate in the test DEB.
"""
import argparse
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', nargs='?', default='release',
                        choices=('release', 'analyze', 'validate-package',
                                 'print-config'))
    parser.add_argument('-j', '--jobs', type=int, default=8)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('jobs must be positive')
    root = pathlib.Path(__file__).resolve().parents[3]
    out = root / 'build/experiments/bulletin-probe/output'
    command = ['make', '-C', str(root / 'source/iOS'), '-j' + str(args.jobs),
               '-f', 'bulletin_probe.mk', 'BUILD_ROOT=' + str(out)]
    if args.target == 'release':
        subprocess.run(command + ['clean'], check=True)
    subprocess.run(command + [args.target], check=True)


if __name__ == '__main__':
    main()
