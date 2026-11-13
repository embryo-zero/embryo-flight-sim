/*
 * SPDX-FileCopyrightText: 2026 James Turner <james@flightgear.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_xmlValueSync.hxx"
#include "config.h"

#include "cppunit/TestAssert.h"
#include "test_suite/FGTestApi/NavDataCache.hxx"
#include "test_suite/FGTestApi/testGlobals.hxx"

#include <simgear/canvas/Canvas.hxx>

#include <Main/fg_commands.hxx>
#include <Main/fg_props.hxx>
#include <Main/globals.hxx>

#include <Canvas/canvas_mgr.hxx>
#include <Canvas/gui_mgr.hxx>

#include "test_suite/FGTestApi/DummyCanvasSystemAdapter.hxx"

#include <GUI/new_gui.hxx>

using namespace std::string_literals;
using namespace flightgear;

extern bool global_nasalMinimalInit;

namespace {

const char* TEST_PROP = "/test/xml-value";

// A minimal widget peer. It does not override valueChanged/apply/etc, so the
// logic in XMLObjectBase is what is exercised.
const char* TEST_WIDGET_NASAL = R"(
var TestValueWidget = {
    init: func(props) {
        me.updateCount = 0;
        me.viewValue = nil;
    },
    postinit: func {
        # a real widget does this when it is shown
        me._localValue = me.value;
        me.viewValue = me.value;
    },
    show: func(viewParent) {
        # the parent group needs a view and a layout item for every child
        me._view = viewParent.createChild("group");
        me._layout = canvas.Spacer.new();
        return me._view;
    },
    update: func {
        me.updateCount += 1;
        me.viewValue = me._localValue;
    },
    getUpdateCount: func { return me.updateCount; },
    getViewValue: func { return me.viewValue; },
    userEdit: func(v) { me._changeLocalValue(v); },
};

globals.gui._createCompatObjectLookupHash["test-value"] = TestValueWidget;
globals.gui.xml.Object.registerClass("test-value", 0);
)";

} // namespace

void XMLValueSyncTests::setUp()
{
    global_nasalMinimalInit = false;

    FGTestApi::setUp::initTestGlobals("xmlvaluesync");
    FGTestApi::setUp::initNavDataCache();

    fgSetBool("/sim/menubar/enable", false);
    fgInitCommands();

    simgear::canvas::Canvas::setSystemAdapter(
        simgear::canvas::SystemAdapterPtr(new canvas::DummyCanvasSystemAdapter));

    auto sm = globals->get_subsystem_mgr();
    sm->add<CanvasMgr>();
    sm->add<NewGUI>();
    sm->add("CanvasGUI", new GUIMgr, SGSubsystemMgr::DISPLAY);

    sm->bind();
    sm->init();

    FGTestApi::setUp::initStandardNasal(true /* withCanvas */);
    sm->postinit();

    CPPUNIT_ASSERT(FGTestApi::executeNasal(TEST_WIDGET_NASAL));
}

void XMLValueSyncTests::tearDown()
{
    _dialog.clear();
    FGTestApi::tearDown::shutdownTestGlobals();
}

SGSharedPtr<FGPUICompatDialog> XMLValueSyncTests::makeDialog(const std::string& prop, bool live)
{
    SGPropertyNode_ptr props = new SGPropertyNode;
    props->setStringValue("name", "value-sync-test");
    props->setStringValue("layout", "vbox");

    auto w = props->addChild("test-value");
    w->setStringValue("name", "w");
    w->setStringValue("property", prop);
    w->setBoolValue("live", live);

    SGSharedPtr<FGPUICompatDialog> dlg(new FGPUICompatDialog(props));
    CPPUNIT_ASSERT(dlg->init());
    _dialog = dlg;
    return dlg;
}

int XMLValueSyncTests::updateCount(const PUICompatObjectRef& w) const
{
    return w->callMethod<int>("getUpdateCount");
}

double XMLValueSyncTests::viewValue(const PUICompatObjectRef& w) const
{
    return w->callMethod<double>("getViewValue");
}

void XMLValueSyncTests::userEdit(const PUICompatObjectRef& w, double v) const
{
    w->callMethod<void, double>("userEdit", v);
}

void XMLValueSyncTests::testInitialValue()
{
    fgSetDouble(TEST_PROP, 3.0);
    auto dlg = makeDialog(TEST_PROP, false);
    auto w = dlg->widgetByName("w");
    CPPUNIT_ASSERT(w);
    CPPUNIT_ASSERT(!w->isLive());

    CPPUNIT_ASSERT_EQUAL(3.0, viewValue(w));
    CPPUNIT_ASSERT_EQUAL(0, updateCount(w));
}

void XMLValueSyncTests::testLiveWidgetUpdatesOnPropertyChange()
{
    fgSetDouble(TEST_PROP, 1.0);
    auto dlg = makeDialog(TEST_PROP, true);
    auto w = dlg->widgetByName("w");
    CPPUNIT_ASSERT(w->isLive());

    dlg->update();
    CPPUNIT_ASSERT_EQUAL(0, updateCount(w)); // nothing changed yet

    fgSetDouble(TEST_PROP, 7.0);
    // the Nasal callback is deferred to the dialog update, to avoid recursion
    CPPUNIT_ASSERT_EQUAL(0, updateCount(w));

    dlg->update();
    CPPUNIT_ASSERT_EQUAL(1, updateCount(w));
    CPPUNIT_ASSERT_EQUAL(7.0, viewValue(w));

    // no further change, no further update
    dlg->update();
    CPPUNIT_ASSERT_EQUAL(1, updateCount(w));
}

void XMLValueSyncTests::testNonLiveWidgetIgnoresPropertyChange()
{
    fgSetDouble(TEST_PROP, 1.0);
    auto dlg = makeDialog(TEST_PROP, false);
    auto w = dlg->widgetByName("w");

    fgSetDouble(TEST_PROP, 7.0);
    dlg->update();
    dlg->update();

    CPPUNIT_ASSERT_EQUAL(0, updateCount(w));
    CPPUNIT_ASSERT_EQUAL(1.0, viewValue(w));
}

void XMLValueSyncTests::testNonLiveWidgetUpdatesAfterUpdateValues()
{
    fgSetDouble(TEST_PROP, 1.0);
    auto dlg = makeDialog(TEST_PROP, false);
    auto w = dlg->widgetByName("w");

    fgSetDouble(TEST_PROP, 7.0);
    dlg->updateValues();
    // like live widgets, the actual update happens on the next dialog update
    CPPUNIT_ASSERT_EQUAL(0, updateCount(w));

    dlg->update();
    CPPUNIT_ASSERT_EQUAL(1, updateCount(w));
    CPPUNIT_ASSERT_EQUAL(7.0, viewValue(w));

    // updateValues with no property change doesn't trigger another update
    dlg->updateValues();
    dlg->update();
    CPPUNIT_ASSERT_EQUAL(1, updateCount(w));
}

void XMLValueSyncTests::testLocalEditNotPropagatedUntilApply()
{
    fgSetDouble(TEST_PROP, 1.0);
    auto dlg = makeDialog(TEST_PROP, false);
    auto w = dlg->widgetByName("w");

    userEdit(w, 5.0);
    dlg->update();
    CPPUNIT_ASSERT_EQUAL(1.0, fgGetDouble(TEST_PROP));

    dlg->applyValues();
    CPPUNIT_ASSERT_EQUAL(5.0, fgGetDouble(TEST_PROP));
}

void XMLValueSyncTests::testUpdateValuesDiscardsLocalEdit()
{
    fgSetDouble(TEST_PROP, 1.0);
    auto dlg = makeDialog(TEST_PROP, false);
    auto w = dlg->widgetByName("w");

    userEdit(w, 5.0);

    // updateValues() is property -> widget: it resets the widget to the
    // property value (like a 'revert'), and never writes to the property
    dlg->updateValues();
    dlg->update();
    CPPUNIT_ASSERT_EQUAL(1.0, fgGetDouble(TEST_PROP));
    CPPUNIT_ASSERT_EQUAL(1.0, viewValue(w));

    // so a following apply has nothing to write
    dlg->applyValues();
    CPPUNIT_ASSERT_EQUAL(1.0, fgGetDouble(TEST_PROP));
}

void XMLValueSyncTests::testLiveLocalEditNotPropagatedUntilApply()
{
    fgSetDouble(TEST_PROP, 1.0);
    auto dlg = makeDialog(TEST_PROP, true);
    auto w = dlg->widgetByName("w");

    userEdit(w, 5.0);
    dlg->update();
    CPPUNIT_ASSERT_EQUAL(1.0, fgGetDouble(TEST_PROP));

    dlg->applyValues();
    CPPUNIT_ASSERT_EQUAL(5.0, fgGetDouble(TEST_PROP));

    // the apply write fires the live listener; the widget re-syncs from it
    dlg->update();
    CPPUNIT_ASSERT_EQUAL(5.0, viewValue(w));
}

void XMLValueSyncTests::testApplyNoChangeDoesNotWrite()
{
    fgSetDouble(TEST_PROP, 1.0);
    auto dlg = makeDialog(TEST_PROP, false);

    // apply() with an unedited widget must not write to the property
    // (which would fire listeners and could clamp values)
    int listenerHits = 0;
    struct Counter : SGPropertyChangeListener {
        int* hits;
        explicit Counter(int* h)
            : hits(h)
        {
        }
        void valueChanged(SGPropertyNode*) override { ++*hits; }
    } counter(&listenerHits);

    auto node = fgGetNode(TEST_PROP, true);
    node->addChangeListener(&counter);

    dlg->applyValues();
    node->removeChangeListener(&counter);

    CPPUNIT_ASSERT_EQUAL(0, listenerHits);
    CPPUNIT_ASSERT_EQUAL(1.0, fgGetDouble(TEST_PROP));
}

void XMLValueSyncTests::testApplySkipsHiddenWidget()
{
    fgSetDouble(TEST_PROP, 1.0);
    auto dlg = makeDialog(TEST_PROP, false);
    auto w = dlg->widgetByName("w");

    userEdit(w, 5.0);
    w->setVisible(false);
    dlg->applyValues();
    CPPUNIT_ASSERT_EQUAL(1.0, fgGetDouble(TEST_PROP));

    w->setVisible(true);
    dlg->applyValues();
    CPPUNIT_ASSERT_EQUAL(5.0, fgGetDouble(TEST_PROP));
}

void XMLValueSyncTests::testApplyByName()
{
    fgSetDouble(TEST_PROP, 1.0);
    auto dlg = makeDialog(TEST_PROP, false);
    auto w = dlg->widgetByName("w");

    userEdit(w, 5.0);
    dlg->applyValues("some-other-widget");
    CPPUNIT_ASSERT_EQUAL(1.0, fgGetDouble(TEST_PROP));

    dlg->applyValues("w");
    CPPUNIT_ASSERT_EQUAL(5.0, fgGetDouble(TEST_PROP));
}
