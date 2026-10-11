// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner

#include "testNasalReload.hxx"

#include "test_suite/FGTestApi/testGlobals.hxx"

#include <Main/fg_props.hxx>
#include <Main/globals.hxx>
#include <Scripting/NasalSys.hxx>

void NasalReloadTests::setUp()
{
    FGTestApi::setUp::initTestGlobals("nasal-reload");

    globals->get_subsystem_mgr()->bind();
    globals->get_subsystem_mgr()->init();

    FGTestApi::setUp::initStandardNasal(false);
    globals->get_subsystem_mgr()->postinit();
}

void NasalReloadTests::tearDown()
{
    FGTestApi::tearDown::shutdownTestGlobals();
}

void NasalReloadTests::testModulesRegistered()
{
    auto nasal = globals->get_props()->getNode("nasal");

    // top-level single file modules, both in the priority list and not
    for (const char* name : {"props", "geo", "aircraft"}) {
        auto n = nasal->getChild(name);
        CPPUNIT_ASSERT_MESSAGE(name, n != nullptr);
        CPPUNIT_ASSERT_MESSAGE(name, n->getBoolValue("loaded"));
        CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(n->getChildren("file").size()));
    }

    // the std module is implemented by a directory, with some files in the
    // priority list: these are loaded in the listed order
    auto stdNode = nasal->getChild("std");
    CPPUNIT_ASSERT(stdNode);
    CPPUNIT_ASSERT(stdNode->getBoolValue("loaded"));

    bool ok = FGTestApi::executeNasal(R"(
        unitTest.assert(std.min(1, 2) == 1, "std.nas replaced by std/minmax.nas");
        unitTest.assert(ishash(std.Vector), "std/Vector.nas loaded");
        unitTest.assert(isfunc(std.all), "std/algorithm.nas loaded");
        unitTest.assert(isfunc(gui.popupTip), "gui/gui.nas loaded");
        unitTest.assert(isfunc(modules.isAvailable), "modules/modules.nas loaded");
    )");
    CPPUNIT_ASSERT(ok);
}

void NasalReloadTests::testReloadDirectoryModule()
{
    auto nasal = globals->get_subsystem<FGNasalSys>();

    bool ok = FGTestApi::executeNasal(R"(
        std.__reloadMarker = 1;
    )");
    CPPUNIT_ASSERT(ok);

    CPPUNIT_ASSERT(nasal->reloadModuleFromFile("std"));

    // the marker is gone (module was recreated), and the contents of the
    // top-level-priority files are all still present
    ok = FGTestApi::executeNasal(R"(
        unitTest.assert(std["__reloadMarker"] == nil, "module was recreated");
        unitTest.assert(std.min(1, 2) == 1, "minmax present after reload");
        unitTest.assert(ishash(std.Hash), "Hash present after reload");
        unitTest.assert(ishash(std.Vector), "Vector present after reload");
        unitTest.assert(isfunc(std.all), "algorithm present after reload");
    )");
    CPPUNIT_ASSERT(ok);
}

void NasalReloadTests::testReloadTopLevelModule()
{
    auto nasal = globals->get_subsystem<FGNasalSys>();

    for (const char* module : {"geo", "aircraft"}) {
        const std::string name{module};
        bool ok = FGTestApi::executeNasal(name + R"(.__reloadMarker = 1;)");
        CPPUNIT_ASSERT(ok);

        CPPUNIT_ASSERT_MESSAGE(name, nasal->reloadModuleFromFile(name));

        ok = FGTestApi::executeNasal(R"(
            unitTest.assert(ishash(globals[")" +
                                     name + R"("]), "module exists after reload");
            unitTest.assert(globals[")" +
                                     name + R"("]["__reloadMarker"] == nil, "module was recreated");
        )");
        CPPUNIT_ASSERT_MESSAGE(name, ok);
        CPPUNIT_ASSERT(globals->get_props()->getBoolValue("nasal/" + name + "/loaded"));
    }
}

void NasalReloadTests::testReloadUnknownModule()
{
    auto nasal = globals->get_subsystem<FGNasalSys>();
    CPPUNIT_ASSERT(!nasal->reloadModuleFromFile("no_such_module"));
}
