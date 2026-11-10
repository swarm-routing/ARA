# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
build_lib_example(
  NAME ara-example
  SOURCE_FILES ara-example.cc
  LIBRARIES_TO_LINK ${libara} ${libswarm-core} ${libwifi} ${libmobility} ${libapplications}
                    ${libflow-monitor}
)
