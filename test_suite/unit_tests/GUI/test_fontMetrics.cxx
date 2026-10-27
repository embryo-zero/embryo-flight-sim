/*
 * SPDX-FileCopyrightText: 2026 James Turner <james@flightgear.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_fontMetrics.hxx"
#include "config.h"

#include "cppunit/TestAssert.h"
#include "test_suite/FGTestApi/testGlobals.hxx"

#include <simgear/canvas/Canvas.hxx>

#include <Canvas/canvas_mgr.hxx>
#include <Main/fg_commands.hxx>
#include <Main/globals.hxx>

#include "test_suite/FGTestApi/DummyCanvasSystemAdapter.hxx"

extern bool global_nasalMinimalInit;

void FontMetricsTests::setUp()
{
    global_nasalMinimalInit = false;
    FGTestApi::setUp::initTestGlobals("font-metrics");
    fgInitCommands();

    simgear::canvas::Canvas::setSystemAdapter(
        simgear::canvas::SystemAdapterPtr(new canvas::DummyCanvasSystemAdapter));

    auto sm = globals->get_subsystem_mgr();
    sm->add<CanvasMgr>();
    sm->bind();
    sm->init();

    FGTestApi::setUp::initStandardNasal(true /* withCanvas */);
    sm->postinit();
}

void FontMetricsTests::tearDown()
{
    FGTestApi::tearDown::shutdownTestGlobals();
}

void FontMetricsTests::testBasicMeasure()
{
    // needs Pango text layout, which can crash on headless runners
    FG_REQUIRE_GRAPHICS();

    bool ok = FGTESTAPI_EXECUTE_NASAL(R"nasal(
        var fm = canvas.FontMetrics.new("Liberation Sans 14px");
        unitTest.assert(fm.ascent() > 0, "ascent");
        unitTest.assert(fm.descent() > 0, "descent");
        unitTest.assert(fm.lineHeight() >= fm.ascent() + fm.descent() - 1, "line height");

        unitTest.assert_equal(fm.horizontalAdvance(""), 0, "empty string");
        var w1 = fm.horizontalAdvance("Hello");
        var w2 = fm.horizontalAdvance("Hello World");
        unitTest.assert(w1 > 0, "non-empty");
        unitTest.assert(w2 > w1, "longer is wider");
        # cached result must be identical
        unitTest.assert_equal(fm.horizontalAdvance("Hello"), w1, "cache hit");
        # space handling: 'a b' must be wider than 'ab'
        unitTest.assert(fm.horizontalAdvance("a b") > fm.horizontalAdvance("ab"), "space has width");

        unitTest.assert_equal(fm.width("Hello"), w1, "width() == horizontalAdvance()");
        unitTest.assert(fm.height("Hello") >= fm.ascent(), "height");

        # compare with the real element
        var cv = canvas.new({size: [256, 256], view: [256, 256]});
        var rt = cv.createGroup().createChild("richtext");
        rt.setFont(canvas.FontDescription.new(family: "Liberation Sans", size: 14));
        rt.setText("Hello World");
        unitTest.assert_equal(rt.width(), w2, "matches RichText width");
    )nasal");
    CPPUNIT_ASSERT(ok);
}

void FontMetricsTests::testFontChange()
{
    // needs Pango text layout, which can crash on headless runners
    FG_REQUIRE_GRAPHICS();

    bool ok = FGTESTAPI_EXECUTE_NASAL(R"nasal(
        var small = canvas.FontDescription.new(family: "Liberation Sans", size: 10);
        var large = canvas.FontDescription.new(family: "Liberation Sans", size: 24);
        var fm = canvas.FontMetrics.new(small);
        var ws = fm.horizontalAdvance("Hello");
        fm.setFont(large);
        unitTest.assert(fm.horizontalAdvance("Hello") > ws, "cache invalidated by font change");

        # a different face of the same family must really be loaded, not the fallback
        var regular = canvas.FontMetrics.new("Liberation Sans 14px");
        var bold = canvas.FontMetrics.new("Liberation Sans Bold 14px");
        unitTest.assert(bold.horizontalAdvance("Hello World") > regular.horizontalAdvance("Hello World"),
            "bold face is wider than regular (font registered)");
    )nasal");
    CPPUNIT_ASSERT(ok);
}

void FontMetricsTests::testBadFont()
{
    // needs Pango text layout, which can crash on headless runners
    FG_REQUIRE_GRAPHICS();

    // non-string / non-FontDescription argument is rejected
    bool ok = FGTESTAPI_EXECUTE_NASAL(R"nasal(
        var err = call(func canvas.FontMetrics.new(42), nil, var e = []);
        unitTest.assert(size(e) > 0, "bad argument raises");
    )nasal");
    CPPUNIT_ASSERT(ok);
}
