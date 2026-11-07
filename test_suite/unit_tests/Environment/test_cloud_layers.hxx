// test_cloud_layers.hxx - unit tests for FGClouds' METAR/scenario cloud layer
// generation, and individually placed (add-cloud) clouds.
//
// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Stuart Buchanan

#pragma once

#include <memory>

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <Environment/fgclouds.hxx>

class CloudLayerTests : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE(CloudLayerTests);
    CPPUNIT_TEST(testDisabledLayersKeepPlacedClouds);
    CPPUNIT_TEST(testDisabledLayersKeepNonRepeatingMode);
    CPPUNIT_TEST(testRemovedCloudOutlivesRebuild);
    CPPUNIT_TEST(testClearLayersRemovesClouds);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp() override;
    void tearDown() override;

    // With /environment/params/cloud-layers-enabled false, a layer rebuild
    // leaves clouds placed via add-cloud in place.
    void testDisabledLayersKeepPlacedClouds();

    // With /environment/params/cloud-layers-enabled false, a layer rebuild
    // does not force the voxel field back to repeating mode.
    void testDisabledLayersKeepNonRepeatingMode();

    // A cloud removed while a background field rebuild is using it is kept
    // alive until the rebuild has finished with it.
    void testRemovedCloudOutlivesRebuild();

    // Setting every layer clear replaces a committed field of layer clouds
    // with an empty one.
    void testClearLayersRemovesClouds();

private:
    // Launches a field rebuild if one is pending, waits for it and commits it.
    void commitPendingRebuild();

    std::unique_ptr<FGClouds> _clouds;
};
