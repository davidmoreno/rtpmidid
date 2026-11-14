#!/bin/sh
# Remove the rtpmidid MPC package. Kept as its own entry point for the docs and
# for people who unpacked the package long ago; the work is done by
# install.sh --uninstall, so there is only one implementation.
exec sh "$(cd "$(dirname "$0")" && pwd)/install.sh" --uninstall "$@"
