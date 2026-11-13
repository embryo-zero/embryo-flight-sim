/*
 * SPDX-FileCopyrightText: 2025 James Turner <james@flightgear.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_itemModel.hxx"
#include "config.h"


#include "cppunit/TestAssert.h"
#include "simgear/props/props.hxx"
#include "simgear/structure/exception.hxx"
#include "test_suite/FGTestApi/NavDataCache.hxx"
#include "test_suite/FGTestApi/testGlobals.hxx"

#include <initializer_list>
#include <set>
#include <simgear/canvas/Canvas.hxx>
#include <simgear/props/props_io.hxx>

#include <Main/fg_commands.hxx>
#include <Main/fg_props.hxx>
#include <Main/globals.hxx>

#include <Canvas/canvas_mgr.hxx>
#include <Canvas/gui_mgr.hxx>

#include "test_suite/FGTestApi/DummyCanvasSystemAdapter.hxx"
#include "test_suite/FGTestApi/testGlobals.hxx"

#include <GUI/AirportListModel.hxx>
#include <GUI/FGPUICompatDialog.hxx>
#include <GUI/ItemModel.hxx>
#include <GUI/NasalItemView.hxx>
#include <GUI/PUICompatObject.hxx>
#include <GUI/PositionedQueryModel.hxx>
#include <Translations/FGTranslate.hxx>

#include <GUI/new_gui.hxx>

#include <Airports/airport.hxx>
#include <Scripting/NasalSys.hxx>

using namespace std::string_literals;
using namespace flightgear;

extern bool global_nasalMinimalInit;

class ModelObserver
{
public:
    struct Event {
        Event(ItemModel::Change c, int r, int count) : change(c),
                                                       row(r),
                                                       count(count)
        {
        }

        ItemModel::Change change;
        int row = -1, count = -1;
    };

    void f(ItemModel::Change t, size_t row, size_t count)
    {
        events.emplace_back(t, row, count);
    }

    std::vector<Event> events;
};

void ItemModelTests::setUp()
{
    global_nasalMinimalInit = false;

    FGTestApi::setUp::initTestGlobals("xmlui", "fr");
    FGTestApi::setUp::initNavDataCache(); // dialog loader uses the cache

    fgSetBool("/sim/menubar/enable", false);

    // Canvas needs loadxml command
    fgInitCommands();

    simgear::canvas::Canvas::setSystemAdapter(
        simgear::canvas::SystemAdapterPtr(new canvas::DummyCanvasSystemAdapter));

    auto sm = globals->get_subsystem_mgr();
    sm->add<CanvasMgr>();
    sm->add<NewGUI>();
    auto canvasGui = new GUIMgr;
    sm->add("CanvasGUI", canvasGui, SGSubsystemMgr::DISPLAY);


    sm->bind();
    sm->init();

    FGTestApi::setUp::initStandardNasal(true /* withCanvas */);

    sm->postinit();
}

void ItemModelTests::tearDown()
{
    FGTestApi::tearDown::shutdownTestGlobals();
}

// trivial model for testing
class TrivialItemModel : public ItemModel
{
public:
    using Item = std::pair<std::string, std::string>;

    TrivialItemModel(const std::initializer_list<Item> items) : m_data(items)
    {
    }

    void insert(int index, const Item& item)
    {
        beginAddRows(index, 1);
        m_data.insert(m_data.begin() + index, item);
        endAddRows();
    }

    size_t count() const override
    {
        return m_data.size();
    }

    std::any dataAt(size_t index, const std::string& key) const override
    {
        if (index >= m_data.size()) {
            throw sg_exception("invalid index");
        }

        if (key == "label") {
            return m_data.at(index).first;
        }

        if (key == "value") {
            return m_data.at(index).second;
        }

        return {};
    }

private:
    std::vector<Item> m_data;
};

void ItemModelTests::testCallbacks()
{
    ItemModelRef m(new TrivialItemModel({{"Apples", "app"},
                                         {"Bananas", "ban"},
                                         {"Apples", "app2"},
                                         {"Carrots", "carrot"}}));

    CPPUNIT_ASSERT_EQUAL(std::any_cast<std::string>(m->dataAt(2, "label")), "Apples"s);

    // invalid indices
    CPPUNIT_ASSERT_THROW(m->dataAt(6, "value"), sg_exception);
    CPPUNIT_ASSERT_THROW(m->dataAt(99, "value"), sg_exception);

    // undefined keys
    CPPUNIT_ASSERT_EQUAL(m->dataAt(2, "velocity").has_value(), false);


    ModelObserver obs;
    m->addChangeCallback([&obs](ItemModel::Change t, size_t row, size_t count) {
        obs.f(t, row, count);
    });
}

void ItemModelTests::testPropertyModel()
{
    SGPropertyNode_ptr props(new SGPropertyNode());
    props->setStringValue("fruit[0]/legend", "Apple");
    props->setIntValue("fruit[0]/shelf", 41);
    props->setStringValue("fruit[2]/legend", "Lemon");
    props->setIntValue("fruit[2]/shelf", 42);
    props->setStringValue("fruit[3]/legend", "Lime");
    props->setIntValue("fruit[3]/shelf", 43);
    props->setStringValue("other/legend", "foofofof");

    SGSharedPtr<PropertyItemModel> m(new PropertyItemModel(props));
    m->setItemName("fruit");
    m->setLabelPath("legend");
    m->setValuePath("shelf");

    CPPUNIT_ASSERT_EQUAL(m->count(), size_t{3});
    CPPUNIT_ASSERT_EQUAL(std::any_cast<std::string>(m->dataAt(2, "label")), "Lime"s);
    CPPUNIT_ASSERT_EQUAL(std::any_cast<int>(m->dataAt(1, "value")), 42);

    CPPUNIT_ASSERT_THROW(m->dataAt(3, "value"), sg_exception);

    // insert updating
    ModelObserver obs;
    m->addChangeCallback([&obs](ItemModel::Change t, size_t row, size_t count) {
        obs.f(t, row, count);
    });

    props->setStringValue("fruit[1]/legend", "Grape");
    CPPUNIT_ASSERT_EQUAL(obs.events.size(), size_t{3});
    const auto ev0 = obs.events.at(0);
    CPPUNIT_ASSERT_EQUAL(ev0.change, ItemModel::Change::RowsWillBeAdded);
    CPPUNIT_ASSERT_EQUAL(ev0.row, 1);
    CPPUNIT_ASSERT_EQUAL(ev0.count, 1);

    const auto ev1 = obs.events.at(1);
    CPPUNIT_ASSERT_EQUAL(ev1.change, ItemModel::Change::RowsAdded);
    CPPUNIT_ASSERT_EQUAL(ev1.row, 1);
    CPPUNIT_ASSERT_EQUAL(ev1.count, 1);

    CPPUNIT_ASSERT_EQUAL(m->count(), size_t{4});
    obs.events.clear();

    props->setIntValue("fruit[1]/shelf", 99);
    const auto ev2 = obs.events.at(0);
    CPPUNIT_ASSERT_EQUAL(ev2.change, ItemModel::Change::Modified);
    CPPUNIT_ASSERT_EQUAL(ev2.row, 1);
    CPPUNIT_ASSERT_EQUAL(ev2.count, 1);

    CPPUNIT_ASSERT_EQUAL(std::any_cast<std::string>(m->dataAt(1, "label")), "Grape"s);

    // existing value updating
    obs.events.clear();
    props->setStringValue("fruit[3]/legend", "Pineapple");
    const auto ev3 = obs.events.at(0);
    CPPUNIT_ASSERT_EQUAL(ev3.change, ItemModel::Change::Modified);
    CPPUNIT_ASSERT_EQUAL(ev3.row, 3);
    CPPUNIT_ASSERT_EQUAL(ev3.count, 1);

    CPPUNIT_ASSERT_EQUAL(std::any_cast<std::string>(m->dataAt(3, "label")), "Pineapple"s);
}

void ItemModelTests::testPropertyModelSyntaxes()
{
    // legacy (ui-version < 2): <value>Blah</value>, label is the value
    auto legacy = FGTestApi::propsFromString(R"(<PropertyList>
        <value>Alpha</value><value>Beta</value><value>Gamma</value></PropertyList>)");

    SGSharedPtr<PropertyItemModel> m(new PropertyItemModel(legacy));
    m->setLabelPath(".");
    m->setValuePath(".");
    m->setItemName("value");

    CPPUNIT_ASSERT_EQUAL(size_t{3}, m->count());
    CPPUNIT_ASSERT_EQUAL("Beta"s, m->labelAt(1));
    CPPUNIT_ASSERT_EQUAL("Beta"s, std::any_cast<std::string>(m->valueAt(1)));

    // ui-version 2: <item><label/><value/></item>
    auto v2 = FGTestApi::propsFromString(R"(<PropertyList>
        <item><label>Alpha</label><value>1</value></item>
        <item><label>Beta</label><value>2</value></item></PropertyList>)");

    SGSharedPtr<PropertyItemModel> m2(new PropertyItemModel(v2));
    m2->setItemName("item");
    m2->setLabelPath("label");
    m2->setValuePath("value");

    CPPUNIT_ASSERT_EQUAL(size_t{2}, m2->count());
    CPPUNIT_ASSERT_EQUAL("Beta"s, m2->labelAt(1));
    CPPUNIT_ASSERT_EQUAL("2"s, std::any_cast<std::string>(m2->valueAt(1)));

    // empty value path falls back to the item node itself
    SGSharedPtr<PropertyItemModel> m3(new PropertyItemModel(legacy));
    m3->setLabelPath(".");
    m3->setItemName("value");
    CPPUNIT_ASSERT_EQUAL("Gamma"s, std::any_cast<std::string>(m3->valueAt(2)));

    // index as value
    m3->setValuePath("#");
    CPPUNIT_ASSERT_EQUAL(size_t{2}, std::any_cast<size_t>(m3->valueAt(2)));
}

void ItemModelTests::testPropertyModelAppend()
{
    // appending past the end, to an initially empty model, and populating
    // the item *after* it is created
    SGPropertyNode_ptr props(new SGPropertyNode());
    SGSharedPtr<PropertyItemModel> m(new PropertyItemModel(props));
    m->setItemName("item");
    m->setLabelPath("label");
    m->setValuePath("value");
    CPPUNIT_ASSERT_EQUAL(size_t{0}, m->count());

    ModelObserver obs;
    m->addChangeCallback([&obs](ItemModel::Change t, size_t row, size_t count) {
        obs.f(t, row, count);
    });

    auto item = props->addChild("item");
    CPPUNIT_ASSERT_EQUAL(size_t{1}, m->count());
    CPPUNIT_ASSERT_EQUAL(ItemModel::Change::RowsWillBeAdded, obs.events.at(0).change);
    CPPUNIT_ASSERT_EQUAL(0, obs.events.at(0).row);
    obs.events.clear();

    // late population of the item still reports a modification
    item->setStringValue("label", "Late");
    item->setStringValue("value", "late-value");
    CPPUNIT_ASSERT(!obs.events.empty());
    CPPUNIT_ASSERT_EQUAL(ItemModel::Change::Modified, obs.events.back().change);
    CPPUNIT_ASSERT_EQUAL("Late"s, m->labelAt(0));

    // append a second
    obs.events.clear();
    props->setStringValue("item[1]/label", "Second");
    CPPUNIT_ASSERT_EQUAL(size_t{2}, m->count());
    CPPUNIT_ASSERT_EQUAL(ItemModel::Change::RowsWillBeAdded, obs.events.at(0).change);
    CPPUNIT_ASSERT_EQUAL(1, obs.events.at(0).row);

    // removal
    obs.events.clear();
    props->removeChild("item", 0);
    CPPUNIT_ASSERT_EQUAL(size_t{1}, m->count());
    CPPUNIT_ASSERT_EQUAL(ItemModel::Change::RowsWillBeRemoved, obs.events.at(0).change);
    CPPUNIT_ASSERT_EQUAL("Second"s, m->labelAt(0));
}

void ItemModelTests::testPropertyModelTranslation()
{
    // relies on the 'options' resource of the French translation, as other tests do
    auto props = FGTestApi::propsFromString(R"(<PropertyList>
        <item><label translate="y">general-options</label><value>a</value></item>
        <item><label>general-options</label><value>b</value></item>
        <item><label translate="y">not-a-real-key</label><value>c</value></item>
        </PropertyList>)");

    SGSharedPtr<PropertyItemModel> m(new PropertyItemModel(props));
    m->setItemName("item");
    m->setLabelPath("label");
    m->setValuePath("value");

    // off by default
    CPPUNIT_ASSERT_EQUAL("general-options"s, m->labelAt(0));

    // enabled but no resource: still untranslated
    m->setAutoTranslate(true);
    CPPUNIT_ASSERT_EQUAL("general-options"s, m->labelAt(0));

    m->setTranslationResource("options");
    CPPUNIT_ASSERT_EQUAL("Options générales"s, m->labelAt(0));
    // not flagged for translation
    CPPUNIT_ASSERT_EQUAL("general-options"s, m->labelAt(1));
    // flagged, but no such key: key is returned
    CPPUNIT_ASSERT_EQUAL("not-a-real-key"s, m->labelAt(2));
    // the key remains accessible
    CPPUNIT_ASSERT_EQUAL("general-options"s, std::any_cast<std::string>(m->dataAt(0, "labelKey")));
    // values never translated
    CPPUNIT_ASSERT_EQUAL("a"s, std::any_cast<std::string>(m->valueAt(0)));

    m->setAutoTranslate(false);
    CPPUNIT_ASSERT_EQUAL("general-options"s, m->labelAt(0));
}

void ItemModelTests::testChangeCallbackIds()
{
    ItemModelRef m(new TrivialItemModel({{"A", "a"}, {"B", "b"}}));
    int calls1 = 0, calls2 = 0, calls3 = 0;
    auto id1 = m->addChangeCallback([&](ItemModel::Change, size_t, size_t) { ++calls1; });
    auto id2 = m->addChangeCallback([&](ItemModel::Change, size_t, size_t) { ++calls2; });
    auto id3 = m->addChangeCallback([&](ItemModel::Change, size_t, size_t) { ++calls3; });
    CPPUNIT_ASSERT(id1 != id2 && id2 != id3);

    // removing the first must not disturb the others
    m->removeChangeCallback(id1);
    static_cast<TrivialItemModel*>(m.get())->insert(0, {"C", "c"});
    CPPUNIT_ASSERT_EQUAL(0, calls1);
    CPPUNIT_ASSERT(calls2 > 0);
    CPPUNIT_ASSERT(calls3 > 0);

    const int before = calls3;
    m->removeChangeCallback(id2);
    m->removeChangeCallback(id2); // harmless
    static_cast<TrivialItemModel*>(m.get())->insert(0, {"D", "d"});
    CPPUNIT_ASSERT(calls3 > before);
}

void ItemModelTests::testNasalPropertyModelListener()
{
    fgSetString("/bar/item[0]/label", "One");
    fgSetString("/bar/item[0]/value", "1");

    bool ok = FGTestApi::executeNasal(R"(
        var r = props.globals.getNode("/bar");
        var m = gui.PropertyItemModel.new(r, "item", "label", "value");
        unitTest.assert_equal(m.count, 1);
        unitTest.assert_equal(m.labelAt(0), "One");
        unitTest.assert_equal(m.valueAt(0), "1");

        var events = [];
        var id = m.addChangeListener(func(type, row, count) {
            append(events, [type, row, count]);
        });

        r.getNode("item[1]/label", 1).setValue("Two");
        unitTest.assert_equal(m.count, 2);
        unitTest.assert_equal(events[0][0], gui.MODEL_CHANGE_ROWS_WILL_BE_ADDED);
        unitTest.assert_equal(events[0][1], 1);
        unitTest.assert_equal(events[1][0], gui.MODEL_CHANGE_ROWS_ADDED);

        m.removeChangeListener(id);
        var n = size(events);
        r.getNode("item[2]/label", 1).setValue("Three");
        unitTest.assert_equal(size(events), n);

        m.autoTranslate = 1;
        m.translationResource = "options";
        m.translationDomain = "core";
    )");

    CPPUNIT_ASSERT(ok);
}

void ItemModelTests::testMenuFromModel()
{
    bool ok = FGTestApi::executeNasal(R"(
        var root = props.globals.getNode("/test-menu-model", 1);
        root.getNode("item[0]/label", 1).setValue("Alpha");
        root.getNode("item[0]/value", 1).setValue("a");
        root.getNode("item[2]/label", 1).setValue("Gamma");
        root.getNode("item[2]/value", 1).setValue("g");

        var model = gui.PropertyItemModel.new(root, "item", "label", "value");

        var menu = canvas.gui.MenuFromModel.new("test-model-menu");
        # fixed items before the model
        menu.createItem("Header one");
        menu.createItem("Header two");

        var picked = [];
        menu.setModel(model, func(row, value, label) {
            append(picked, [row, value, label]);
        });

        unitTest.assert_equal(menu.count(), 4);
        unitTest.assert_equal(menu.getItem(0).text(), "Header one");
        unitTest.assert_equal(menu.getItem(2).text(), "Alpha");
        unitTest.assert_equal(menu.getItem(3).text(), "Gamma");

        # index <-> row mapping
        unitTest.assert_equal(menu.modelRowForIndex(1), nil);
        unitTest.assert_equal(menu.modelRowForIndex(3), 1);
        unitTest.assert_equal(menu.indexForModelRow(0), 2);

        # insertion in the middle of the model
        root.getNode("item[1]/label", 1).setValue("Beta");
        root.getNode("item[1]/value", 1).setValue("b");
        unitTest.assert_equal(menu.count(), 5);
        unitTest.assert_equal(menu.getItem(3).text(), "Beta");
        unitTest.assert_equal(menu.getItem(4).text(), "Gamma");

        # label changes
        root.getNode("item[1]/label").setValue("Beta2");
        unitTest.assert_equal(menu.getItem(3).text(), "Beta2");

        # clicking reports the row, value and label
        var it = menu.getItem(3);
        call(it._cb, [nil], it._cb_me);
        unitTest.assert_equal(size(picked), 1);
        unitTest.assert_equal(picked[0][0], 1);
        unitTest.assert_equal(picked[0][1], "b");
        unitTest.assert_equal(picked[0][2], "Beta2");

        # removal
        root.removeChild("item", 0);
        unitTest.assert_equal(menu.count(), 4);
        unitTest.assert_equal(menu.getItem(2).text(), "Beta2");
        unitTest.assert_equal(menu.getItem(0).text(), "Header one");

        # a click after rows moved reports the new row
        it = menu.getItem(2);
        call(it._cb, [nil], it._cb_me);
        unitTest.assert_equal(picked[1][0], 0);

        # a trailing fixed item, added after the model
        menu.createItem("Other...");
        unitTest.assert_equal(menu.count(), 5);
        root.getNode("item[5]/label", 1).setValue("Omega");
        unitTest.assert_equal(menu.getItem(4).text(), "Omega");
        unitTest.assert_equal(menu.getItem(5).text(), "Other...");

        # detaching removes the model items only, and stops listening
        menu.setModel(nil);
        unitTest.assert_equal(menu.count(), 3);
        unitTest.assert_equal(menu.getItem(0).text(), "Header one");
        root.getNode("item[6]/label", 1).setValue("Ignored");
        unitTest.assert_equal(menu.count(), 3);
    )");

    CPPUNIT_ASSERT(ok);
}

void ItemModelTests::testComboBoxWithModel()
{
    bool ok = FGTestApi::executeNasal(R"(
        var root = props.globals.getNode("/test-combo-model", 1);
        root.getNode("value[0]", 1).setValue("one");
        root.getNode("value[1]", 1).setValue("two");

        var c = canvas.new({size: [200, 100], view: [200, 100]});
        var combo = canvas.gui.widgets.ComboBox.new(c.createGroup(), canvas.style, {});

        # a fixed, selectable item, then the model
        combo.createItem("Fixed", "fixed");
        var model = gui.PropertyItemModel.new(root, "value", ".", ".");
        combo.setModel(model);

        unitTest.assert_equal(combo._itemCount(), 3);
        unitTest.assert_equal(combo.findByValue("two"), 2);
        unitTest.assert_equal(combo.findByText("one"), 1);
        unitTest.assert_equal(combo.findByValue("fixed"), 0);

        combo.setSelectedByValue("two");
        unitTest.assert_equal(combo._currentIndex, 2);

        # a click on a model item selects it
        var it = combo.menu().getItem(1);
        call(it._cb, [nil], it._cb_me);
        unitTest.assert_equal(combo._currentIndex, 1);
        unitTest.assert_equal(combo._selectedValue, "one");

        # selection follows its value as rows move
        root.getNode("value[0]").setValue("zero");
        root.getNode("value[1]").setValue("one");
        root.getNode("value[2]", 1).setValue("three");
        unitTest.assert_equal(combo._itemCount(), 4);
        unitTest.assert_equal(combo.findByValue("three"), 3);

        # a selected value which is removed leaves the selection pending, and
        # it is picked up again if it reappears
        root.removeChild("value", 1);
        unitTest.assert_equal(combo._currentIndex, nil);
        unitTest.assert_equal(combo._selectedValue, "one");
        root.getNode("value[1]", 1).setValue("one");
        unitTest.assert_equal(combo._currentIndex, combo.findByValue("one"));

        # detaching
        combo.setModel(nil);
        unitTest.assert_equal(combo._itemCount(), 1);
        root.getNode("value[9]", 1).setValue("ignored");
        unitTest.assert_equal(combo._itemCount(), 1);
    )");

    CPPUNIT_ASSERT(ok);
}

void ItemModelTests::testXMLComboBoxSyntaxes()
{
    // the label of the first item is translated via the 'dialog-exit'
    // resource, since dialogs use their name to select that.
    const std::string translated = FGTranslate("core").getWithDefault("dialog-exit", "exit-button-label", "");
    CPPUNIT_ASSERT(!translated.empty());
    CPPUNIT_ASSERT(translated != "exit-button-label");
    fgSetString("/test/combo/expected-label", translated);

    // ui-version 2 <properties> lists use item children with label and value
    fgSetString("/test/combo/avail/item[0]/label", "Runway A");
    fgSetString("/test/combo/avail/item[0]/value", "rwy-a");
    fgSetString("/test/combo/avail/item[1]/label", "Runway B");
    fgSetString("/test/combo/avail/item[1]/value", "rwy-b");
    // ui-version < 2 uses plain value children
    fgSetString("/test/combo/plain/value[0]", "plain-a");
    fgSetString("/test/combo/plain/value[1]", "plain-b");
    fgSetString("/test/combo/v2", "go2");
    fgSetString("/test/combo/v1", "beta");

    // ui-version 2: <item> with label and value, plus a <properties> combo
    auto v2Props = FGTestApi::propsFromString(R"(<PropertyList>
        <name>exit</name>
        <layout>vbox</layout>
        <ui-version>2</ui-version>
        <nasal><ready>
            globals.comboTest = {
                items: dialog.findWidget("combo-items")._view,
                avail: dialog.findWidget("combo-avail")._view
            };
        </ready></nasal>
        <combo>
            <name>combo-items</name>
            <property>/test/combo/v2</property>
            <live>true</live>
            <item><label translate="y">exit-button-label</label><value>go</value></item>
            <item><label>exit-button-label</label><value>go2</value></item>
        </combo>
        <combo>
            <name>combo-avail</name>
            <property>/test/combo/rwy</property>
            <properties>/test/combo/avail</properties>
        </combo>
    </PropertyList>)");

    SGSharedPtr<FGPUICompatDialog> v2(new FGPUICompatDialog(v2Props));
    CPPUNIT_ASSERT(v2->init());

    bool ok = FGTestApi::executeNasal(R"(
        var t = globals.comboTest;
        var items = t.items;
        unitTest.assert_equal(items._itemCount(), 2);
        unitTest.assert_equal(items.findByValue("go"), 0);
        unitTest.assert_equal(items.findByText(getprop("/test/combo/expected-label")), 0);
        # not marked for translation, so left alone
        unitTest.assert_equal(items.findByText("exit-button-label"), 1);

        # the property's value was selected
        unitTest.assert_equal(items._currentIndex, 1);
        setprop("/test/combo/v2", "go");
    )");
    CPPUNIT_ASSERT(ok);

    // property changes are applied by the dialog's update, not immediately
    v2->update();

    ok = FGTestApi::executeNasal(R"(
        var t = globals.comboTest;
        var items = t.items;
        unitTest.assert_equal(items._currentIndex, 0);

        # items from <properties>, which can change, and have separate labels
        var avail = t.avail;
        unitTest.assert_equal(avail._itemCount(), 2);
        unitTest.assert_equal(avail.findByText("Runway B"), 1);
        unitTest.assert_equal(avail.findByValue("rwy-b"), 1);
        unitTest.assert_equal(avail.findByText("rwy-b"), -1);

        var availNode = props.globals.getNode("/test/combo/avail");
        availNode.getNode("item[2]/label", 1).setValue("Runway C");
        availNode.getNode("item[2]/value", 1).setValue("rwy-c");
        unitTest.assert_equal(avail._itemCount(), 3);
        unitTest.assert_equal(avail.findByValue("rwy-c"), 2);
        availNode.removeChild("item", 0);
        unitTest.assert_equal(avail._itemCount(), 2);
        unitTest.assert_equal(avail.findByValue("rwy-a"), -1);
    )");
    CPPUNIT_ASSERT(ok);
    v2.clear();

    // ui-version < 2: <value> children
    auto v1Props = FGTestApi::propsFromString(R"(<PropertyList>
        <name>combo-v1</name>
        <layout>vbox</layout>
        <nasal><ready>
            globals.comboTest = {
                values: dialog.findWidget("combo-values")._view,
                plain: dialog.findWidget("combo-plain")._view
            };
        </ready></nasal>
        <combo>
            <name>combo-values</name>
            <property>/test/combo/v1</property>
            <value>alpha</value>
            <value>beta</value>
            <value>gamma</value>
        </combo>
        <combo>
            <name>combo-plain</name>
            <property>/test/combo/plain-selected</property>
            <properties>/test/combo/plain</properties>
        </combo>
    </PropertyList>)");

    SGSharedPtr<FGPUICompatDialog> v1(new FGPUICompatDialog(v1Props));
    CPPUNIT_ASSERT(v1->init());

    ok = FGTestApi::executeNasal(R"(
        var values = globals.comboTest.values;
        unitTest.assert_equal(values._itemCount(), 3);
        unitTest.assert_equal(values.findByText("gamma"), 2);
        unitTest.assert_equal(values.findByValue("beta"), 1);
        unitTest.assert_equal(values._currentIndex, 1);

        # <properties> with plain value children, label and value the same
        var plain = globals.comboTest.plain;
        unitTest.assert_equal(plain._itemCount(), 2);
        unitTest.assert_equal(plain.findByText("plain-b"), 1);
        unitTest.assert_equal(plain.findByValue("plain-b"), 1);
    )");
    CPPUNIT_ASSERT(ok);
}

void ItemModelTests::testNasalPropertyModel()
{
    fgSetString("/foo/vehicle[0]/name", "Ford");
    fgSetInt("/foo/vehicle[0]/id", 10);
    fgSetString("/foo/vehicle[1]/name", "Volkswagen");
    fgSetInt("/foo/vehicle[1]/id", 14);
    fgSetString("/foo/vehicle[4]/name", "Mercedes");
    fgSetInt("/foo/vehicle[4]/id", -100);

    bool ok = FGTestApi::executeNasal(R"(
        var r = props.globals.getNode("/foo");
        var m = gui.PropertyItemModel.new(r, "vehicle");
        m.labelPath = "name";
        m.valuePath = "id";

        unitTest.assert_equal(m.count, 3);
        unitTest.assert_equal(m.dataAt(2, "label"), "Mercedes");
        unitTest.assert_equal(m.dataAt(1, "value"), 14);

        r.getNode("vehicle[2]/name", 1).setValue("Audi");
        unitTest.assert_equal(m.count, 4);
        unitTest.assert_equal(m.dataAt(2, "label"), "Audi");
        unitTest.assert_equal(m.dataAt(3, "label"), "Mercedes");
    )");

    CPPUNIT_ASSERT(ok);
}

void ItemModelTests::testNasalModel()
{
    bool ok = FGTestApi::executeNasal(R"(
        var TestNasalModel = {
            _new: func()
            {
                var m = {
                    parents: [TestNasalModel],
                    labels: ['Apples', 'Bananas', 'Carrots'],
                    values: ['app1', 'bn', 'car']
                };
                return m;
            },

            count: func()
            {
                return size(me.labels);
            },

            modelData: func(row, key)
            {
                if (key == "label") return me.labels[row];
                if (key == "value") return me.values[row];
                return nil;
            }
        };

        var bm = TestNasalModel._new();
        var m = gui.NasalItemModel.new(bm);

        unitTest.assert_equal(m.count, 3);
        unitTest.assert_equal(m.dataAt(1, "label"), "Bananas");

    )");

    CPPUNIT_ASSERT(ok);
}

void ItemModelTests::testItemView()
{
    bool ok = FGTestApi::executeNasal(R"(
        var TestNasalModel = {
            _new: func()
            {
                var m = {
                    parents: [TestNasalModel],
                    labels: ['Apples', 'Bananas', 'Carrots', 'Donkeys', 'Elephants', 'Figs', 'Grapes', 'Horses', 'Ibis', 'Jackals', 'Kiwis', 'Llamas', 'Monkeys', 'Newts'],
                    values: ['app1', 'bn', 'car', 'don', 'ele', 'fig', 'gra', 'hor', 'ibi', 'jac', 'kiw', 'lla', 'mon', 'new'],
                    mass: [1.0, 1.1, 0.5, 200.0, 5000.0, 0.2, 0.3, 400.0, 2.0, 15.0, 0.4, 150.0, 30.0, 0.1]
                };
                return m;
            },

            count: func()
            {
                return size(me.labels);
            },

            modelData: func(row, key)
            {
                if (key == "label") return me.labels[row];
                if (key == "value") return me.values[row];
                if (key == "mass") return me.mass[row];
                return nil;
            },

            changeRow: func(row, label, value = nil, mass = nil)
            {
                me.labels[row] = label;
                if (value != nil) {
                    me.values[row] = value;
                }
                if (mass != nil) {
                    me.mass[row] = mass;
                }
                me.modelDataChanged(row, 1);
            }
        };

        var bm = TestNasalModel._new();
        var model = gui.NasalItemModel.new(bm);

        var TestView = {
            _new: func()
            {
                var m = {
                    parents: [TestView],
                    _delegateCount: 0,
                    _boundDelegateCount: 0
                };
                return m;
            },

            createDelegate: func()
            {
                var d = {
                    parents: [],
                    boundIndex: -1,
                    dataChangedCalled: false,
                    movedCalled: false,
                    unbindCalled: false,
                    text: "",

                    bind: func(index, modelData)
                    {
                        me.model = modelData;
                        me.view = modelData.view;
                        me.boundIndex = index;
                        modelData.view._boundDelegateCount += 1;
                        me.text = modelData.label;
                        logprint(LOG_INFO, "Delegate for index:" ~ index ~ " bound with label:" ~ me.text);
                    },

                    dataChanged: func()
                    {
                        logprint(LOG_INFO, "Delegate for index:" ~ me.boundIndex ~ " dataChanged called");
                        me.dataChangedCalled = true;
                        me.text = me.model.label;
                    },

                    moved: func()
                    {
                        me.movedCalled = true;
                    },

                    unbind: func()
                    {
                        logprint(LOG_INFO, "Delegate for index:" ~ me.boundIndex ~ " unbound");
                        me.unbindCalled = true;
                        me.model.view._boundDelegateCount -= 1;
                    }
                };

                me._delegateCount += 1;
                return d;
            },

            modelReset: func()
            {
            },

            visibleRowsChanged: func()
            {
            },

            scrollBarChanged: func()
            {
            },

            dataChanged: func(row)
            {
                logprint(LOG_INFO, "View dataChanged called for row:" ~ row);
            }
        };

        var v = gui.ItemView.new(TestView._new(), model);
        v.viewHeight = 320;
        v.delegateHeight = 60;
        v.cacheHeight = 0;

       # unitTest.assert_equal(6, v._delegateCount);
        var d2 = v.delegateForIndex(2);
        unitTest.assert_equal(d2.boundIndex, 2);
        unitTest.assert_equal(d2.model.index, 2);
        unitTest.assert_equal(d2.model.yPosition, 120);

        var d5 = v.delegateForIndex(5);
        unitTest.assert_equal(d5.boundIndex, 5);
        unitTest.assert_equal(d5.model.index, 5);
        unitTest.assert_equal(d5.model.yPosition, 300);

        unitTest.assert_equal(d5.text, "Figs");

        model.changeRow(5, "Ferrets", "frts", 3.0);
        unitTest.assert_equal(d5.text, "Ferrets");

        # scrolling
        v.viewOffset = 130;
        v.dumpDelegates();

        unitTest.assert_equal(6, v._boundDelegateCount);
        unitTest.assert_equal(nil, v.delegateForIndex(0));
        unitTest.assert_equal(nil, v.delegateForIndex(1));

        var d8 = v.delegateForIndex(8);
        unitTest.assert_equal(nil, d8);
        unitTest.assert_equal(nil, d8);

        unitTest.assert_equal(nil, v.delegateForIndex(9));

        unitTest.assert_equal(2, v.indexForViewPosition(5));
        unitTest.assert_equal(7, v.indexForViewPosition(310));

        v.viewOffset = 250;
        unitTest.assert_equal(nil, v.delegateForIndex(3));
        var d9 = v.delegateForIndex(9);
        unitTest.assert_equal(d9.boundIndex, 9);
        unitTest.assert_equal(d9.model.index, 9);
        unitTest.assert_equal(d9.model.yPosition, 540);
        unitTest.assert_equal(d9.text, "Jackals");
        unitTest.assert_equal(6, v._boundDelegateCount);
       # unitTest.assert_equal(6, v._delegateCount);

        v.dumpDelegates();

        # reset model to empty: all bound delegates must have unbind() called
        bm.labels = [];
        bm.values = [];
        bm.mass = [];
        model.reset();

        unitTest.assert_equal(0, v._boundDelegateCount);
        unitTest.assert_equal(nil, v.delegateForIndex(0));
    )");

    CPPUNIT_ASSERT(ok);
}

void ItemModelTests::testAirportListModelSearch()
{
    SGSharedPtr<AirportListModel> m(new AirportListModel);

    // A term shorter than 3 characters should leave the model empty without crashing.
    m->setSearchTerm("ED");
    CPPUNIT_ASSERT_EQUAL(size_t{0}, m->count());

    // Partial ICAO prefix "EDD" should match the family of German airports
    // (EDDF Frankfurt, EDDM Munich, EDDL Düsseldorf, …).
    m->setSearchTerm("EDD");
    CPPUNIT_ASSERT(m->count() > 0);

    // The label is "ICAO name" – both parts must be non-empty.
    for (size_t i = 0; i < m->count(); ++i) {
        auto label = std::any_cast<std::string>(m->dataAt(i, "label"));
        CPPUNIT_ASSERT(!label.empty());
    }

    // Well-known airports must be present.
    bool foundEDDF = false, foundEDDM = false;
    for (size_t i = 0; i < m->count(); ++i) {
        auto icao = std::any_cast<std::string>(m->dataAt(i, "icao"));
        if (icao == "EDDF") foundEDDF = true;
        if (icao == "EDDM") foundEDDM = true;
    }
    CPPUNIT_ASSERT_MESSAGE("EDDF (Frankfurt) must appear in EDD search", foundEDDF);
    CPPUNIT_ASSERT_MESSAGE("EDDM (Munich) must appear in EDD search", foundEDDM);

    // Changing to an unrelated term resets the results.
    m->setSearchTerm("ZZZZ_UNLIKELY");
    // May or may not be empty, but should not crash; count() must be defined.
    (void)m->count();
}

void ItemModelTests::testAirportListModelNameSearch()
{
    SGSharedPtr<AirportListModel> m(new AirportListModel);

    // "castle" should find airports whose name contains "castle"
    // (e.g. Newcastle Airport EGNT, Castle Donington EGNX, …).
    m->setSearchTerm("castle");
    CPPUNIT_ASSERT_MESSAGE("'castle' search should return at least one airport",
                           m->count() > 0);

    for (size_t i = 0; i < m->count(); ++i) {
        auto label = std::any_cast<std::string>(m->dataAt(i, "label"));
        std::string lower = label;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        CPPUNIT_ASSERT_MESSAGE("label should contain 'castle': " + label,
                               lower.find("castle") != std::string::npos);
    }

    // "new" should find airports such as Newcastle, New Orleans, Newark, etc.
    m->setSearchTerm("new");
    CPPUNIT_ASSERT_MESSAGE("'new' search should return at least one airport",
                           m->count() > 0);

    bool foundEGNT = false; // Newcastle Airport
    bool foundKEWR = false; // Newark Liberty International Airport
    bool foundKNEW = false; // New Orleans Lakefront Airport

    for (size_t i = 0; i < m->count(); ++i) {
        auto label = std::any_cast<std::string>(m->dataAt(i, "label"));
        std::string lower = label;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        CPPUNIT_ASSERT_MESSAGE("label should contain 'new': " + label,
                               lower.find("new") != std::string::npos);

        auto icao = std::any_cast<std::string>(m->dataAt(i, "icao"));
        if (icao == "EGNT") foundEGNT = true;
        if (icao == "KEWR") foundKEWR = true;
        if (icao == "KNEW") foundKNEW = true;
    }
    CPPUNIT_ASSERT_MESSAGE("EGNT (Newcastle) must appear in 'new' search", foundEGNT);
    CPPUNIT_ASSERT_MESSAGE("KEWR (Newark) must appear in 'new' search", foundKEWR);
    CPPUNIT_ASSERT_MESSAGE("KNEW (New Orleans) must appear in 'new' search", foundKNEW);
}

void ItemModelTests::testAirportListModelRecents()
{
    SGSharedPtr<AirportListModel> m(new AirportListModel);

    // No search term, no recents: model should be empty.
    CPPUNIT_ASSERT_EQUAL(size_t{0}, m->count());

    // Populate recents.
    FGAirportRef egnt = FGAirport::findByIdent("EGNT"); // Newcastle
    FGAirportRef eddf = FGAirport::findByIdent("EDDF"); // Frankfurt
    CPPUNIT_ASSERT_MESSAGE("EGNT must be in nav data", egnt != nullptr);
    CPPUNIT_ASSERT_MESSAGE("EDDF must be in nav data", eddf != nullptr);
    m->addRecentEntry(egnt);
    m->addRecentEntry(eddf);

    // Drive the model into a non-empty state first so that clearing the
    // search term is treated as a change and triggers useRecentAirports().
    m->setSearchTerm("EDD");
    CPPUNIT_ASSERT(m->count() > 0);

    // Clearing the search term must show the recent airports.
    ModelObserver obs;
    m->addChangeCallback([&obs](ItemModel::Change t, size_t row, size_t count) {
        obs.f(t, row, count);
    });

    m->setSearchTerm("");
    CPPUNIT_ASSERT_EQUAL(size_t{2}, m->count());

    // The recents callback must have fired a Reset.
    CPPUNIT_ASSERT(!obs.events.empty());
    CPPUNIT_ASSERT_EQUAL(ItemModel::Change::Reset, obs.events.front().change);

    // Entries must appear in the order they were added.
    CPPUNIT_ASSERT_EQUAL("EGNT"s, std::any_cast<std::string>(m->dataAt(0, "icao")));
    CPPUNIT_ASSERT_EQUAL("EDDF"s, std::any_cast<std::string>(m->dataAt(1, "icao")));

    // Labels must be non-empty and contain the ICAO code.
    auto label0 = std::any_cast<std::string>(m->dataAt(0, "label"));
    CPPUNIT_ASSERT(label0.find("EGNT") != std::string::npos);
    auto label1 = std::any_cast<std::string>(m->dataAt(1, "label"));
    CPPUNIT_ASSERT(label1.find("EDDF") != std::string::npos);
}

namespace {

// index of the first row with the given ident, or -1
int rowWithIdent(const ItemModel& m, const std::string& ident, const std::string& type = {})
{
    for (size_t i = 0; i < m.count(); ++i) {
        if (!type.empty() && std::any_cast<std::string>(m.dataAt(i, "type")) != type) {
            continue;
        }
        if (std::any_cast<std::string>(m.dataAt(i, "ident")) == ident) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

void ItemModelTests::testPositionedQueryModelSearch()
{
    SGSharedPtr<PositionedQueryModel> m(new PositionedQueryModel);

    // no search term, or fewer than three characters, gives no results
    CPPUNIT_ASSERT_EQUAL(size_t{0}, m->count());
    m->setSearchTerm("CL");
    CPPUNIT_ASSERT_EQUAL(size_t{0}, m->count());

    ModelObserver obs;
    m->addChangeCallback([&obs](ItemModel::Change t, size_t row, size_t count) {
        obs.f(t, row, count);
    });

    // Clacton VOR
    m->setSearchTerm("CLN");
    CPPUNIT_ASSERT(m->count() > 0);
    CPPUNIT_ASSERT(!obs.events.empty());
    CPPUNIT_ASSERT_EQUAL(ItemModel::Change::Reset, obs.events.back().change);

    const int cln = rowWithIdent(*m, "CLN");
    CPPUNIT_ASSERT_MESSAGE("CLN must be found by ident", cln >= 0);

    // lower-case is accepted for idents
    m->setSearchTerm("cln");
    CPPUNIT_ASSERT(rowWithIdent(*m, "CLN") >= 0);

    // going back to a short term empties the model again
    m->setSearchTerm("CL");
    CPPUNIT_ASSERT_EQUAL(size_t{0}, m->count());

    // out of range index gives an empty value
    CPPUNIT_ASSERT(!m->dataAt(100, "label").has_value());
}

void ItemModelTests::testPositionedQueryModelTypes()
{
    SGSharedPtr<PositionedQueryModel> m(new PositionedQueryModel);

    // navaid: label is 'ident (name)', value is the PositionedID
    m->setSearchTerm("CLN");
    int row = rowWithIdent(*m, "CLN", "VOR");
    CPPUNIT_ASSERT_MESSAGE("CLN VOR must be found", row >= 0);

    const auto label = std::any_cast<std::string>(m->dataAt(row, "label"));
    const auto name = std::any_cast<std::string>(m->dataAt(row, "name"));
    CPPUNIT_ASSERT(!name.empty());
    CPPUNIT_ASSERT_EQUAL("CLN ("s + name + ")"s, label);

    const auto guid = std::any_cast<PositionedID>(m->valueAt(row));
    auto pos = FGPositioned::loadById<FGPositioned>(guid);
    CPPUNIT_ASSERT(pos);
    CPPUNIT_ASSERT_EQUAL("CLN"s, pos->ident());

    // waypoint: label is just the ident
    m->setSearchTerm("ROKME");
    row = rowWithIdent(*m, "ROKME");
    CPPUNIT_ASSERT_MESSAGE("ROKME fix must be found", row >= 0);
    CPPUNIT_ASSERT_EQUAL("ROKME"s, std::any_cast<std::string>(m->dataAt(row, "label")));
    CPPUNIT_ASSERT_EQUAL("fix"s, std::any_cast<std::string>(m->dataAt(row, "type")));

    // disabling waypoints removes fixes, but not navaids
    m->setQueryWaypoints(false);
    CPPUNIT_ASSERT_EQUAL(-1, rowWithIdent(*m, "ROKME"));
    m->setSearchTerm("CLN");
    CPPUNIT_ASSERT(rowWithIdent(*m, "CLN") >= 0);

    // and disabling navaids removes the VOR
    m->setQueryNavaids(false);
    CPPUNIT_ASSERT_EQUAL(size_t{0}, m->count()); // neither type enabled
    m->setQueryWaypoints(true);
    for (size_t i = 0; i < m->count(); ++i) {
        CPPUNIT_ASSERT(std::any_cast<std::string>(m->dataAt(i, "type")) != "VOR"s);
    }

    m->setSearchTerm("ROKME");
    CPPUNIT_ASSERT(rowWithIdent(*m, "ROKME") >= 0);

    // VOR / NDB can be selected separately
    m->setQueryWaypoints(false);
    m->setQueryVOR(true);
    m->setQueryNDB(false);
    m->setSearchTerm("CLN");
    CPPUNIT_ASSERT(rowWithIdent(*m, "CLN", "VOR") >= 0);
    CPPUNIT_ASSERT_EQUAL(-1, rowWithIdent(*m, "CLN", "NDB"));
}

void ItemModelTests::testPositionedQueryModelNameSearch()
{
    SGSharedPtr<PositionedQueryModel> m(new PositionedQueryModel);

    // the name is 'CLACTON VOR-DME', matched case-insensitively from the start
    m->setSearchTerm("clacton");
    const int row = rowWithIdent(*m, "CLN");
    CPPUNIT_ASSERT_MESSAGE("CLN must be found by name", row >= 0);

    for (size_t i = 0; i < m->count(); ++i) {
        const auto name = std::any_cast<std::string>(m->dataAt(i, "name"));
        const auto ident = std::any_cast<std::string>(m->dataAt(i, "ident"));
        CPPUNIT_ASSERT_MESSAGE("name or ident should match: " + name,
                               name.find("CLACTON") == 0 || ident.find("CLACTON") == 0);
    }

    // a term matching both ident and name must not give duplicate rows
    m->setSearchTerm("CLN");
    int clnCount = 0;
    for (size_t i = 0; i < m->count(); ++i) {
        if (std::any_cast<std::string>(m->dataAt(i, "ident")) == "CLN") {
            clnCount++;
        }
    }
    CPPUNIT_ASSERT(clnCount >= 1);
    std::set<PositionedID> ids;
    for (size_t i = 0; i < m->count(); ++i) {
        CPPUNIT_ASSERT(ids.insert(std::any_cast<PositionedID>(m->valueAt(i))).second);
    }
}

void ItemModelTests::testPositionedQueryModelSorting()
{
    SGSharedPtr<PositionedQueryModel> m(new PositionedQueryModel);

    auto assertSorted = [&m]() {
        double last = -1.0;
        for (size_t i = 0; i < m->count(); ++i) {
            const double d = std::any_cast<double>(m->dataAt(i, "distanceNm"));
            CPPUNIT_ASSERT(d >= last);
            last = d;
        }
    };

    // default is the aircraft position: put it close to Clacton
    FGTestApi::setPosition(SGGeod::fromDeg(1.15, 51.85));
    m->setSearchTerm("CL");
    m->setSearchTerm("CLA");
    CPPUNIT_ASSERT(m->count() > 0);
    assertSorted();
    CPPUNIT_ASSERT(std::any_cast<double>(m->dataAt(0, "distanceNm")) < 50.0);

    // explicit position elsewhere changes the distances, and keeps the order sorted
    const double nearDist = std::any_cast<double>(m->dataAt(0, "distanceNm"));
    m->setPositionLatLon(37.6, -122.4); // San Francisco
    assertSorted();
    CPPUNIT_ASSERT(std::any_cast<double>(m->dataAt(0, "distanceNm")) > nearDist);

    // clearing returns to the aircraft position
    m->clearPosition();
    assertSorted();
    CPPUNIT_ASSERT_DOUBLES_EQUAL(nearDist, std::any_cast<double>(m->dataAt(0, "distanceNm")), 0.01);
}
