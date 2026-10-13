// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner

#include "testNasalReload.hxx"

#include "test_suite/FGTestApi/testGlobals.hxx"

#include <Main/fg_props.hxx>
#include <Main/globals.hxx>
#include <Scripting/NasalModule.hxx>
#include <Scripting/NasalSys.hxx>

#include <algorithm>

#include <simgear/io/iostreams/sgstream.hxx>
#include <simgear/misc/sg_dir.hxx>
#include <simgear/misc/sg_path.hxx>
#include <simgear/props/props_io.hxx>

namespace {
SGPath overlayDir;

void writeFile(const SGPath& p, const std::string& content)
{
    sg_ofstream f(p);
    f << content;
}
} // namespace

void NasalReloadTests::setUp()
{
    FGTestApi::setUp::initTestGlobals("nasal-reload");

    globals->get_subsystem_mgr()->bind();
    globals->get_subsystem_mgr()->init();

    overlayDir = globals->get_fg_home() / "nasal-overlay";
    simgear::Dir(overlayDir).create(0700);

    writeFile(overlayDir / "extra_geo.nas", "var overlayExtraGeo = 42;\n");
    writeFile(overlayDir / "aircraft_mod.nas", "var overlayValue = 7;\n");
    // extends the core multiplayer module: must load after all the core
    // modules, so markerpins (which loads after multiplayer) already exists
    writeFile(overlayDir / "extra_mp.nas",
              "var overlayMpSawView = isfunc(globals.markerpins.update_pins);\n"
              "var overlayMpSawOwn = isfunc(check_messages);\n"
              // like the C182: depends on the aircraft module listed before it
              "var overlayMpSawAircraftMod = ishash(globals[\"aircraft_mod\"]) and globals.aircraft_mod.overlayValue == 7;\n");

    // extends a non-core module, which is early in the load order: must still
    // load after every built-in module, including input_helpers at the end
    writeFile(overlayDir / "extra_fuel.nas",
              "var overlayFuelSawLater = ishash(globals[\"input_helpers\"]);\n");

    // simulate an aircraft -set.xml, which is read on top of defaults.xml
    // before Nasal is initialised
    const std::string xml =
        "<PropertyList><nasal>"
        "<aircraft_mod><file>" +
        (overlayDir / "aircraft_mod.nas").utf8Str() + "</file></aircraft_mod>"
                                                      "<multiplayer><file>" +
        (overlayDir / "extra_mp.nas").utf8Str() + "</file></multiplayer>"
                                                  "<fuel><file>" +
        (overlayDir / "extra_fuel.nas").utf8Str() + "</file></fuel>"
                                                    "<geo><file>" +
        (overlayDir / "extra_geo.nas").utf8Str() + "</file></geo>"
                                                   "</nasal></PropertyList>";
    writeFile(overlayDir / "aircraft-set.xml", xml);
    readProperties(overlayDir / "aircraft-set.xml", globals->get_props());

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
    for (const char* name : {"props", "markerpins", "aircraft"}) {
        // built-in top-level modules are in nodes with a prefix, so they are
        // not merged with aircraft nodes using the module name
        auto n = nasal->getChild(std::string{"builtin-"} + name);
        CPPUNIT_ASSERT_MESSAGE(name, n != nullptr);
        CPPUNIT_ASSERT_MESSAGE(name, n->getBoolValue("loaded"));
        auto m = globals->get_subsystem<FGNasalSys>()->module(name);
        CPPUNIT_ASSERT_MESSAGE(name, m);
        // files named in defaults.xml are listed; others are found by scanning
        const bool isListed = std::string{name} != "aircraft";
        CPPUNIT_ASSERT_EQUAL_MESSAGE(name, isListed, m->hasListedFiles());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(name, 1, static_cast<int>(m->files().size()));
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

void NasalReloadTests::testListedAndScannedFiles()
{
    auto sys = globals->get_subsystem<FGNasalSys>();

    // directory listed in full in defaults.xml
    auto stdModule = sys->module("std");
    CPPUNIT_ASSERT(stdModule);
    CPPUNIT_ASSERT(stdModule->hasListedFiles());
    CPPUNIT_ASSERT(stdModule->files().size() > 1);

    // only the file named in defaults.xml is loaded, the rest of the directory is skipped
    auto modulesModule = sys->module("modules");
    CPPUNIT_ASSERT(modulesModule);
    CPPUNIT_ASSERT(modulesModule->hasListedFiles());
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(modulesModule->files().size()));

    // a directory module with an entry in defaults.xml but no <file>:
    // the files found in its directory are loaded
    auto failureMgr = sys->module("FailureMgr");
    CPPUNIT_ASSERT(failureMgr);
    CPPUNIT_ASSERT(!failureMgr->hasListedFiles());
    CPPUNIT_ASSERT(!failureMgr->files().empty());
    CPPUNIT_ASSERT(failureMgr->isLoaded());

    // and the same for a top-level file not named in defaults.xml
    auto aircraft = sys->module("aircraft");
    CPPUNIT_ASSERT(aircraft);
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aircraft->files().size()));
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
        CPPUNIT_ASSERT(nasal->module(name)->isLoaded());
    }
}

void NasalReloadTests::testReloadUnknownModule()
{
    auto nasal = globals->get_subsystem<FGNasalSys>();
    CPPUNIT_ASSERT(!nasal->reloadModuleFromFile("no_such_module"));
}

void NasalReloadTests::testOverlayModules()
{
    auto nasal = globals->get_props()->getNode("nasal");

    // the aircraft module comes after all the built-in ones
    std::vector<SGPropertyNode*> children;
    for (int i = 0; i < nasal->nChildren(); ++i) {
        children.push_back(nasal->getChild(i));
    }
    CPPUNIT_ASSERT(!children.empty());
    auto position = [&](SGPropertyNode* n) {
        return std::find(children.begin(), children.end(), n) - children.begin();
    };
    CPPUNIT_ASSERT(position(nasal->getChild("aircraft_mod")) > position(nasal->getChild("builtin-markerpins")));
    CPPUNIT_ASSERT(position(nasal->getChild("aircraft_mod")) > position(nasal->getChild("FailureMgr")));
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(nasal->getChildren("aircraft_mod").size()));

    // built-in modules are still before it, in defaults.xml order
    CPPUNIT_ASSERT(nasal->getChild("builtin-props") == children.front());

    // The aircraft nodes are not merged with the built-in ones with the same
    // module name, so they keep the order of the -set.xml: aircraft_mod,
    // multiplayer, fuel, geo
    auto aircraftNode = [&](const char* name) {
        auto n = nasal->getChild(name);
        CPPUNIT_ASSERT_MESSAGE(name, n);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(name, 1, static_cast<int>(n->getChildren("file").size()));
        return n;
    };
    CPPUNIT_ASSERT(position(aircraftNode("aircraft_mod")) < position(aircraftNode("multiplayer")));
    CPPUNIT_ASSERT(position(aircraftNode("multiplayer")) < position(aircraftNode("fuel")));
    CPPUNIT_ASSERT(position(aircraftNode("fuel")) < position(aircraftNode("geo")));
    CPPUNIT_ASSERT(position(nasal->getChild("multiplayer")) > position(nasal->getChild("FailureMgr")));

    // the built-in files are named in defaults.xml, and not in the property tree
    auto sys = globals->get_subsystem<FGNasalSys>();
    for (const char* name : {"multiplayer", "geo"}) {
        auto m = sys->module(name);
        CPPUNIT_ASSERT_MESSAGE(name, m);
        CPPUNIT_ASSERT_MESSAGE(name, m->hasListedFiles());
        CPPUNIT_ASSERT_MESSAGE(name, m->isLoaded());
        CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(m->files().size()));
        CPPUNIT_ASSERT_EQUAL(0, static_cast<int>(m->extraFiles().size()));
        CPPUNIT_ASSERT_EQUAL(std::string{"builtin-"} + name, std::string{m->node()->getNameString()});
    }

    auto aircraftMod = sys->module("aircraft_mod");
    CPPUNIT_ASSERT(aircraftMod);
    CPPUNIT_ASSERT(!aircraftMod->hasListedFiles());
    CPPUNIT_ASSERT(aircraftMod->isLoaded());
    CPPUNIT_ASSERT(aircraftMod->files().empty());
    CPPUNIT_ASSERT_EQUAL(1, static_cast<int>(aircraftMod->extraFiles().size()));
    CPPUNIT_ASSERT(nasal->getBoolValue("aircraft_mod/loaded"));

    bool ok = FGTestApi::executeNasal(R"(
        unitTest.assert(aircraft_mod.overlayValue == 7, "aircraft module loaded");
        unitTest.assert(geo.overlayExtraGeo == 42, "overlay file merged into geo");
        unitTest.assert(fuel.overlayFuelSawLater, "overlay file for a non-core module loads after all built-in modules");
        unitTest.assert(multiplayer.overlayMpSawView, "overlay file for a core module loads after all core modules");
        unitTest.assert(multiplayer.overlayMpSawAircraftMod, "aircraft nodes load in the order of the -set.xml");
        unitTest.assert(multiplayer.overlayMpSawOwn, "overlay file shares the namespace of the core module");
        unitTest.assert(isfunc(geo.put_model), "geo's own file still loaded");
    )");
    CPPUNIT_ASSERT(ok);
}
