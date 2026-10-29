# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 James Turner

target_sources(fgfs_test_suite PRIVATE
    TestSuite.cxx
    test_cloud_shade.cxx
    test_cloud_shade.hxx
    test_cloud_layers.cxx
    test_cloud_layers.hxx
    test_environment_cache.cxx
    test_environment_cache.hxx
)

fg_add_test_suite(EnvironmentCacheTests u)
fg_add_test_suite(CloudShadeTests u)
fg_add_test_suite(CloudLayerTests u)
