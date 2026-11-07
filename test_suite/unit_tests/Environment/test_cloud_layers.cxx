// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Stuart Buchanan
// test_cloud_layers.cxx - unit tests for FGClouds' METAR/scenario cloud layer
// generation, and individually placed (add-cloud) clouds.

#include "config.h"

#include "test_cloud_layers.hxx"

#include "test_suite/FGTestApi/scene_graph.hxx"
#include "test_suite/FGTestApi/testGlobals.hxx"

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>

#include <Environment/fgclouds.hxx>
#include <Main/fg_props.hxx>
#include <Main/globals.hxx>
#include <Time/light.hxx>
#include <Viewer/renderer.hxx>

#include <simgear/props/props.hxx>
#include <simgear/scene/sky/newcloud.hxx>
#include <simgear/scene/sky/sky.hxx>
#include <simgear/structure/commands.hxx>

namespace {

// A small procedural layer cloud, which needs no voxel texture on disk.
bool addLayerCloud(int index)
{
    SGPropertyNode_ptr args(new SGPropertyNode);
    args->setStringValue("name", "test-layer");
    args->setIntValue("index", index);
    args->setDoubleValue("lon-deg", -3.0);
    args->setDoubleValue("lat-deg", 56.0);
    args->setDoubleValue("alt-ft", 3000.0);
    args->setDoubleValue("coverage-norm", 0.5);
    args->setDoubleValue("thickness-m", 400.0);
    return globals->get_commands()->execute("add-cloud", args);
}

// Lets a test hold a cloud in the middle of being added to a voxel field.
struct CloudGate {
    std::mutex m;
    std::condition_variable cv;
    bool entered = false;
    bool released = false;
    bool destroyed = false;
    bool destroyedWhileInUse = false;
};

// A cloud that blocks in addCloudToDetailedVoxelField() until released,
// and records its own destruction.
class BlockingCloud : public SGVoxelCloud
{
public:
    explicit BlockingCloud(CloudGate& gate)
        : _gate(gate)
    {
    }

    ~BlockingCloud() override
    {
        std::lock_guard<std::mutex> lk(_gate.m);
        _gate.destroyed = true;
    }

    int addCloudToDetailedVoxelField(ImageRef, float, osg::Vec3f) const override
    {
        std::unique_lock<std::mutex> lk(_gate.m);
        _gate.entered = true;
        _gate.cv.notify_all();
        _gate.cv.wait(lk, [this] { return _gate.released; });
        _gate.destroyedWhileInUse = _gate.destroyed;
        return 0;
    }

    int addCloudToRoughVoxelField(ImageRef, float, osg::Vec3f) const override { return 0; }

private:
    CloudGate& _gate;
};

bool deleteCloud(int index)
{
    SGPropertyNode_ptr args(new SGPropertyNode);
    args->setIntValue("index", index);
    return globals->get_commands()->execute("del-cloud", args);
}

} // namespace

void CloudLayerTests::setUp()
{
    FGTestApi::setUp::initTestGlobals("cloud-layers");
    _clouds.reset(new FGClouds);
    _clouds->Init();
}

void CloudLayerTests::tearDown()
{
    _clouds.reset();
    FGTestApi::tearDown::shutdownTestGlobals();
}

void CloudLayerTests::commitPendingRebuild()
{
    _clouds->updateFromOsgTraversal();
    if (_clouds->_rebuildFuture.valid())
        _clouds->_rebuildFuture.wait();
    _clouds->updateFromOsgTraversal();
}

void CloudLayerTests::testDisabledLayersKeepPlacedClouds()
{
    fgSetBool("/environment/params/cloud-layers-enabled", false);
    CPPUNIT_ASSERT(addLayerCloud(1));

    _clouds->set_update_event(1);

    // del-cloud only succeeds for a cloud that is still placed.
    CPPUNIT_ASSERT(deleteCloud(1));
}

void CloudLayerTests::testDisabledLayersKeepNonRepeatingMode()
{
    fgSetBool("/environment/params/cloud-layers-enabled", false);
    _clouds->setCloudsRepeating(false);

    _clouds->set_update_event(1);

    CPPUNIT_ASSERT(!_clouds->isCloudsRepeating());
}

void CloudLayerTests::testRemovedCloudOutlivesRebuild()
{
    // A small field keeps the rebuild quick.
    fgSetInt("/sim/rendering/hdr/clouds/detailed-voxel-field-width", 16);
    fgSetInt("/sim/rendering/hdr/clouds/detailed-voxel-field-height", 8);
    globals->get_subsystem_mgr()->add<FGLight>();

    CloudGate gate;
    SGGeod loc = globals->get_view_position();
    loc.setElevationM(1000.0);
    CPPUNIT_ASSERT(_clouds->addCloud(std::make_unique<BlockingCloud>(gate), 1, loc, 0.0f, 0.0f));

    auto rebuild = std::async(std::launch::async, &FGClouds::runRebuild, _clouds->captureSnapshot());

    bool entered;
    {
        std::unique_lock<std::mutex> lk(gate.m);
        entered = gate.cv.wait_for(lk, std::chrono::seconds(10), [&] { return gate.entered; });
    }

    const bool removed = _clouds->removeCloud(1);

    // Record the state while the rebuild is still using the cloud, then
    // always release it, so a failure can't leave the rebuild blocked.
    bool destroyedOnRemove;
    {
        std::lock_guard<std::mutex> lk(gate.m);
        destroyedOnRemove = gate.destroyed;
        gate.released = true;
    }
    gate.cv.notify_all();

    rebuild.get();

    CPPUNIT_ASSERT(entered);
    CPPUNIT_ASSERT(removed);
    CPPUNIT_ASSERT(!destroyedOnRemove);
    CPPUNIT_ASSERT(!gate.destroyedWhileInUse);
    CPPUNIT_ASSERT(gate.destroyed);
}

void CloudLayerTests::testClearLayersRemovesClouds()
{
    // A real sky with its cloud layers, and the layer definitions from
    // Environment/cloudlayers.xml.
    FGTestApi::setUp::initScenery();
    globals->get_subsystem_mgr()->add<FGLight>();

    // A small field keeps the rebuilds quick.
    fgSetInt("/sim/rendering/hdr/clouds/detailed-voxel-field-width", 16);
    fgSetInt("/sim/rendering/hdr/clouds/detailed-voxel-field-height", 16);

    // Layer clouds are placed at the aircraft, and the field is centred on
    // the view, so put both in the same place.
    SGGeod view = globals->get_view_position();
    fgSetDouble("/position/latitude-deg", view.getLatitudeDeg());
    fgSetDouble("/position/longitude-deg", view.getLongitudeDeg());

    // A broken mid-level layer with no precipitation is altostratus, which
    // is a procedural layer cloud, so needs no voxel textures.
    fgSetDouble("/environment/rain-norm", 0.0);
    fgSetDouble("/environment/snow-norm", 0.0);
    SGPropertyNode* layer0 = fgGetNode("/environment/clouds/layer", 0, true);
    layer0->setDoubleValue("elevation-ft", 8000.0);
    layer0->setStringValue("coverage", "broken");

    _clouds->set_update_event(1);
    commitPendingRebuild();
    CPPUNIT_ASSERT_EQUAL(std::string("as"), layer0->getStringValue("layer-type"));
    // The cloud base is 99999 m only for a field with no clouds in it.
    CPPUNIT_ASSERT((double)_clouds->_cloudBaseM < 99999.0);

    const int layerCount = globals->get_renderer()->getSky()->get_cloud_layer_count();
    for (int i = 0; i < layerCount; i++)
        fgGetNode("/environment/clouds/layer", i, true)->setStringValue("coverage", "clear");

    _clouds->set_update_event(2);
    commitPendingRebuild();
    CPPUNIT_ASSERT_DOUBLES_EQUAL(99999.0, (double)_clouds->_cloudBaseM, 1.0);
}
