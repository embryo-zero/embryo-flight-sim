# SPDX-FileCopyrightText: 2010 James Turner
# SPDX-License-Identifier: GPL-2.0-or-later

target_sources(fgfs_test_suite PRIVATE
    TestSuite.cxx
    test_xmlDialog.cxx test_xmlDialog.hxx
    test_nasalMenu.cxx test_nasalMenu.hxx
    test_itemModel.cxx test_itemModel.hxx
    test_fontMetrics.cxx test_fontMetrics.hxx
    test_xmlValueSync.cxx test_xmlValueSync.hxx
    test_flightPlanModel.cxx test_flightPlanModel.hxx
)

#fg_add_test_suite(XMLDialogTests u)
fg_add_test_suite(NasalMenuTests u)
fg_add_test_suite(ItemModelTests u)
fg_add_test_suite(FontMetricsTests u)
fg_add_test_suite(XMLValueSyncTests u)
fg_add_test_suite(FlightPlanModelTests u)
