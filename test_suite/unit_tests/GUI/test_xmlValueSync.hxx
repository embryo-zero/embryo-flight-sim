/*
 * SPDX-FileComment: Unit tests for XML dialog property <-> widget value flow
 * SPDX-FileCopyrightText: 2026 James Turner <james@flightgear.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <GUI/FGPUICompatDialog.hxx>
#include <GUI/PUICompatObject.hxx>

/**
 * Tests the value synchronisation logic in XMLObjectBase (XMLDialog.nas)
 * and PUICompatObject.cxx, using a dummy Nasal widget peer which records
 * how often update() is called, and what 'view' value it was given.
 */
class XMLValueSyncTests : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE(XMLValueSyncTests);
    CPPUNIT_TEST(testInitialValue);
    CPPUNIT_TEST(testLiveWidgetUpdatesOnPropertyChange);
    CPPUNIT_TEST(testNonLiveWidgetIgnoresPropertyChange);
    CPPUNIT_TEST(testNonLiveWidgetUpdatesAfterUpdateValues);
    CPPUNIT_TEST(testLocalEditNotPropagatedUntilApply);
    CPPUNIT_TEST(testUpdateValuesDiscardsLocalEdit);
    CPPUNIT_TEST(testLiveLocalEditNotPropagatedUntilApply);
    CPPUNIT_TEST(testApplyNoChangeDoesNotWrite);
    CPPUNIT_TEST(testApplySkipsHiddenWidget);
    CPPUNIT_TEST(testApplyByName);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp();
    void tearDown();

    void testInitialValue();
    void testLiveWidgetUpdatesOnPropertyChange();
    void testNonLiveWidgetIgnoresPropertyChange();
    void testNonLiveWidgetUpdatesAfterUpdateValues();
    void testLocalEditNotPropagatedUntilApply();
    void testUpdateValuesDiscardsLocalEdit();
    void testLiveLocalEditNotPropagatedUntilApply();
    void testApplyNoChangeDoesNotWrite();
    void testApplySkipsHiddenWidget();
    void testApplyByName();

private:
    /// create and init a dialog containing one test widget, named "w"
    SGSharedPtr<FGPUICompatDialog> makeDialog(const std::string& prop, bool live);

    // helpers to query the Nasal peer
    int updateCount(const PUICompatObjectRef& w) const;
    double viewValue(const PUICompatObjectRef& w) const;
    void userEdit(const PUICompatObjectRef& w, double v) const;

    SGSharedPtr<FGPUICompatDialog> _dialog;
};
