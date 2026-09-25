# FlipperOS overrides on top of Unleashed's fbt_options.py.
# Unleashed exec()s this file at the end of its own options, so everything
# defined there (FIRMWARE_APPS, ...) is already available here.

FIRMWARE_ORIGIN = "FlipperOS"

# The DIST_SUFFIX environment variable still takes precedence (used by CI)
DIST_SUFFIX = "flipperos-local"

# Put the Flipper OS toolkit into the main menu of the default build
FIRMWARE_APPS["default"].append("flipper_os")
