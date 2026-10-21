// test_cloud_shade.hxx - unit tests for the voxel cloud shade texture
// (FGClouds::runRebuild(), FGClouds::runShadeRebuild() and
// FGClouds::computeColumnShadeChannels()).
//
// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Stuart Buchanan

#pragma once

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

class CloudShadeTests : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE(CloudShadeTests);
    CPPUNIT_TEST(testSkyOpticalDepth);
    CPPUNIT_TEST(testGroundOpticalDepth);
    CPPUNIT_TEST(testHeightWithinSingleRun);
    CPPUNIT_TEST(testHeightWithinTwoRuns);
    CPPUNIT_TEST(testEmptyColumn);
    CPPUNIT_TEST(testHeightDilatesSideways);
    CPPUNIT_TEST(testSunChannelUntouched);
    CPPUNIT_TEST(testSunShadowFromCloudDirectlyAbove);
    CPPUNIT_TEST(testScatteredOverOvercastSunShadow);
    CPPUNIT_TEST(testScatteredOverOvercastColumnChannels);
    CPPUNIT_TEST(testScatteredOverOvercastObliqueSun);
    CPPUNIT_TEST(testScatteredOverOvercastLowSun);
    CPPUNIT_TEST(testFieldRebuildIncludesShade);
    CPPUNIT_TEST(testFieldRebuildShadeUsesSnapshotSun);
    CPPUNIT_TEST(testEmptyFieldRebuildIncludesShade);
    CPPUNIT_TEST(testRepeatingFieldHasEmptyRoughShade);
    CPPUNIT_TEST(testRoughShadeOpticalDepthScale);
    CPPUNIT_TEST(testRoughCloudShadowsDetailedField);
    CPPUNIT_TEST(testShadeRebuildIncludesRoughField);
    CPPUNIT_TEST(testSunMarchStopsWhenSaturated);
    CPPUNIT_TEST(testSaturatedRayIgnoresRoughField);
    CPPUNIT_TEST_SUITE_END();

public:
    // G accumulates optical depth from the top of the field down to and
    // including each voxel.
    void testSkyOpticalDepth();

    // B accumulates optical depth from the bottom of the field up to the
    // centre of each voxel.
    void testGroundOpticalDepth();

    // A runs from near 0 at a cloud's base voxel to near 1 at its top voxel,
    // with air below the cloud 0 and air above it 1.
    void testHeightWithinSingleRun();

    // Each separate run of cloud in a column is normalised independently, and
    // air between them takes the side of whichever run is nearer.
    void testHeightWithinTwoRuns();

    // A column with no cloud is all zero in G, B and A.
    void testEmptyColumn();

    // Air beside a cloud takes the height of the adjacent cloud voxels at the
    // same level, so filtering across the cloud's sides keeps its profile.
    void testHeightDilatesSideways();

    // The R channel (sun optical depth) belongs to the sun march and is not
    // written.
    void testSunChannelUntouched();

    // With the sun overhead, a cloud stacked directly on top of another adds
    // exactly its own optical depth to the sun optical depth (R) of the cloud
    // below, and the cloud below does not change R for the one above.
    void testSunShadowFromCloudDirectlyAbove();

    // A scattered layer above an overcast layer, sun overhead: the overcast is
    // shadowed only beneath the scattered cells, and the overcast does not
    // shadow the scattered layer.
    void testScatteredOverOvercastSunShadow();

    // The sun-independent channels for the same scene: sky optical depth (G)
    // of the overcast includes any scattered cell above it, ground optical
    // depth (B) of the scattered layer includes the overcast below it, and each
    // layer has its own height-within-cloud (A) profile.
    void testScatteredOverOvercastColumnChannels();

    // With the sun at 45 degrees, the scattered layer's shadow falls on the
    // overcast offset towards the sun's azimuth by the layers' vertical
    // separation, not directly beneath it.
    void testScatteredOverOvercastObliqueSun();

    // With a low sun (18.4 degrees), the shadow falls three times the layers'
    // vertical separation away, following the true sun elevation.
    void testScatteredOverOvercastLowSun();

    // A field rebuild returns the shade texture for the field it built, so the
    // two can be committed together: it matches a shade-only rebuild of the new
    // field, and already contains the upper cloud's shadow on the lower one.
    void testFieldRebuildIncludesShade();

    // The field rebuild shades with the sun direction captured in its snapshot.
    void testFieldRebuildShadeUsesSnapshotSun();

    // A rebuild with no clouds in range still returns a (clear-sky) shade
    // texture, replacing any previous field's shading.
    void testEmptyFieldRebuildIncludesShade();

    // A repeating field has no rough field, so its rough shade texture is a
    // single clear-sky voxel.
    void testRepeatingFieldHasEmptyRoughShade();

    // Rough voxels are larger than detailed ones by the rough voxel size
    // factor, so they hold that much more optical depth at the same density.
    void testRoughShadeOpticalDepthScale();

    // A sun ray leaving the detailed field through its side carries on through
    // the rough field, so a cloud only in the rough field shadows the detailed
    // field at low sun.
    void testRoughCloudShadowsDetailedField();

    // A shade-only rebuild of a non-repeating field rebuilds both shade
    // textures, matching those from the field rebuild.
    void testShadeRebuildIncludesRoughField();

    // The sun march stops once a voxel is fully shadowed (optical depth over
    // 5), leaving unsaturated voxels exact.
    void testSunMarchStopsWhenSaturated();

    // A detailed sun ray that saturates inside the detailed field does not
    // also pick up the rough field's shade, as it never reached the edge.
    void testSaturatedRayIgnoresRoughField();
};
