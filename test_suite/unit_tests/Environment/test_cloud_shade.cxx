// test_cloud_shade.cxx - unit tests for the voxel cloud shade texture.
//
// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Stuart Buchanan

#include "config.h"

#include "test_cloud_shade.hxx"

#include <cstring>
#include <vector>

#include <Environment/fgclouds.hxx>

namespace {

const float TOLERANCE = 1.0e-5f;

// Tolerance for sun optical depths, which accumulate many float steps.
const float SUN_TOLERANCE = 1.0e-4f;

const osg::Vec3f SUN_OVERHEAD(0.0f, 0.0f, 1.0f);

// A width x width x height RGBA float voxel field and matching shade buffer.
struct Field {
    Field(int width, int height)
        : width(width),
          height(height),
          voxels((size_t)width * width * height * 4, 0.0f),
          shade((size_t)width * width * height * 4, 0.0f)
    {
    }

    size_t index(int i, int j, int k) const
    {
        return (((size_t)k * width + j) * width + i) * 4;
    }

    void setDensity(int i, int j, int k, float density) { voxels[index(i, j, k) + 2] = density; }

    // Fill the inclusive block [i0, i1] x [j0, j1] x [k0, k1].
    void setBlock(int i0, int i1, int j0, int j1, int k0, int k1, float density)
    {
        for (int k = k0; k <= k1; ++k)
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i)
                    setDensity(i, j, k, density);
    }

    float sun(int i, int j, int k) const { return shade[index(i, j, k)]; }
    float sky(int i, int j, int k) const { return shade[index(i, j, k) + 1]; }
    float ground(int i, int j, int k) const { return shade[index(i, j, k) + 2]; }
    float height_(int i, int j, int k) const { return shade[index(i, j, k) + 3]; }

    void compute(float voxelOpticalDepth = 1.0f)
    {
        FGClouds::computeColumnShadeChannels(voxels.data(), shade.data(), width, height, voxelOpticalDepth);
    }

    // Run the full shade rebuild (sun march plus column channels).
    void rebuild(const osg::Vec3f& sunDir, float voxelOpticalDepth = 1.0f)
    {
        FGClouds::ShadeSnapshot snap;
        snap.voxelData = voxels;
        snap.width = width;
        snap.height = height;
        snap.voxelOpticalDepth = voxelOpticalDepth;
        snap.sunDirVoxel = sunDir;
        auto result = FGClouds::runShadeRebuild(snap);
        std::memcpy(shade.data(), result.voxelShadeData->data(), shade.size() * sizeof(float));
    }

    int width;
    int height;
    std::vector<float> voxels;
    std::vector<float> shade;
};

} // namespace

void CloudShadeTests::testSkyOpticalDepth()
{
    Field f(1, 4);
    f.setDensity(0, 0, 1, 0.5f);
    f.setDensity(0, 0, 2, 1.0f);
    f.compute(2.0f);

    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, f.sky(0, 0, 3), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(2.0, f.sky(0, 0, 2), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(3.0, f.sky(0, 0, 1), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(3.0, f.sky(0, 0, 0), TOLERANCE);
}

void CloudShadeTests::testGroundOpticalDepth()
{
    Field f(1, 4);
    f.setDensity(0, 0, 1, 0.5f);
    f.setDensity(0, 0, 2, 1.0f);
    f.compute(2.0f);

    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, f.ground(0, 0, 0), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, f.ground(0, 0, 1), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(2.0, f.ground(0, 0, 2), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(3.0, f.ground(0, 0, 3), TOLERANCE);
}

void CloudShadeTests::testHeightWithinSingleRun()
{
    Field f(1, 8);
    for (int k = 2; k <= 5; ++k) f.setDensity(0, 0, k, 1.0f);
    f.compute();

    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, f.height_(0, 0, 0), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, f.height_(0, 0, 1), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.125, f.height_(0, 0, 2), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.375, f.height_(0, 0, 3), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.625, f.height_(0, 0, 4), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.875, f.height_(0, 0, 5), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, f.height_(0, 0, 6), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, f.height_(0, 0, 7), TOLERANCE);
}

void CloudShadeTests::testHeightWithinTwoRuns()
{
    // Runs at k = 0..1 and k = 6, with air at k = 2..5 between them.
    Field f(1, 8);
    f.setDensity(0, 0, 0, 1.0f);
    f.setDensity(0, 0, 1, 1.0f);
    f.setDensity(0, 0, 6, 0.2f);
    f.compute();

    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.25, f.height_(0, 0, 0), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.75, f.height_(0, 0, 1), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, f.height_(0, 0, 6), TOLERANCE);

    // Nearer the lower run's top
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, f.height_(0, 0, 2), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, f.height_(0, 0, 3), TOLERANCE);
    // Nearer the upper run's base
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, f.height_(0, 0, 5), TOLERANCE);
    // Above the upper run
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, f.height_(0, 0, 7), TOLERANCE);
}

void CloudShadeTests::testEmptyColumn()
{
    Field f(2, 4);
    f.setDensity(1, 1, 2, 1.0f); // cloud only in a diagonal neighbour's column
    f.compute();

    for (int k = 0; k < 4; ++k) {
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, f.sky(0, 0, k), TOLERANCE);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, f.ground(0, 0, k), TOLERANCE);
    }
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, f.height_(0, 0, 0), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, f.height_(0, 0, 3), TOLERANCE);
}

void CloudShadeTests::testHeightDilatesSideways()
{
    // A 2-voxel-tall cloud in the centre column of a 3x3 field.
    Field f(3, 4);
    f.setDensity(1, 1, 1, 1.0f);
    f.setDensity(1, 1, 2, 1.0f);
    f.compute();

    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            CPPUNIT_ASSERT_DOUBLES_EQUAL(0.25, f.height_(i, j, 1), TOLERANCE);
            CPPUNIT_ASSERT_DOUBLES_EQUAL(0.75, f.height_(i, j, 2), TOLERANCE);
        }
    }

    // Levels with no cloud beside them fall back to the column rule: the
    // neighbouring columns are empty, so 0.
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, f.height_(0, 0, 3), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, f.height_(1, 1, 3), TOLERANCE);
}

void CloudShadeTests::testSunChannelUntouched()
{
    Field f(1, 4);
    f.setDensity(0, 0, 1, 1.0f);
    for (int k = 0; k < 4; ++k) f.shade[f.index(0, 0, k)] = 42.0f;
    f.compute();

    for (int k = 0; k < 4; ++k) {
        CPPUNIT_ASSERT_DOUBLES_EQUAL(42.0, f.shade[f.index(0, 0, k)], TOLERANCE);
    }
}

void CloudShadeTests::testSunShadowFromCloudDirectlyAbove()
{
    // Two 2-voxel-deep clouds in one column, the upper one resting directly on
    // the lower one.
    Field lowerOnly(4, 8), upperOnly(4, 8), both(4, 8);
    lowerOnly.setBlock(1, 1, 1, 1, 1, 2, 1.0f);
    upperOnly.setBlock(1, 1, 1, 1, 3, 4, 1.0f);
    both.setBlock(1, 1, 1, 1, 1, 2, 1.0f);
    both.setBlock(1, 1, 1, 1, 3, 4, 1.0f);
    for (Field* f : {&lowerOnly, &upperOnly, &both}) f->rebuild(SUN_OVERHEAD);

    // The upper cloud's optical depth (2 voxels x 1.0) is added to the lower
    // cloud's shadow.
    for (int k = 1; k <= 2; ++k) {
        CPPUNIT_ASSERT_DOUBLES_EQUAL(lowerOnly.sun(1, 1, k) + 2.0, both.sun(1, 1, k), SUN_TOLERANCE);
    }

    // The upper cloud is unaffected by the cloud below it.
    for (int k = 3; k <= 4; ++k) {
        CPPUNIT_ASSERT_DOUBLES_EQUAL(upperOnly.sun(1, 1, k), both.sun(1, 1, k), SUN_TOLERANCE);
    }

    // Shadow deepens monotonically down through the stack.
    for (int k = 1; k <= 4; ++k) {
        CPPUNIT_ASSERT(both.sun(1, 1, k) > both.sun(1, 1, k + 1));
    }
}

namespace {

// An 8 x 8 x 8 field with a 1-voxel-deep overcast layer at k = 1 across the
// whole field, and/or a single 2 x 2 x 2 scattered cell at i, j = 2..3,
// k = 4..5 above it.
const int SCATTERED_I0 = 2, SCATTERED_I1 = 3, SCATTERED_K0 = 4, SCATTERED_K1 = 5;
const int OVERCAST_K = 1;

void addOvercast(Field& f) { f.setBlock(0, f.width - 1, 0, f.width - 1, OVERCAST_K, OVERCAST_K, 1.0f); }

void addScattered(Field& f)
{
    f.setBlock(SCATTERED_I0, SCATTERED_I1, SCATTERED_I0, SCATTERED_I1, SCATTERED_K0, SCATTERED_K1, 1.0f);
}

} // namespace

void CloudShadeTests::testScatteredOverOvercastSunShadow()
{
    Field overcastOnly(8, 8), scatteredOnly(8, 8), both(8, 8);
    addOvercast(overcastOnly);
    addScattered(scatteredOnly);
    addOvercast(both);
    addScattered(both);
    for (Field* f : {&overcastOnly, &scatteredOnly, &both}) f->rebuild(SUN_OVERHEAD);

    // Overcast beneath a scattered cell carries the cell's optical depth
    // (2 voxels x 1.0) on top of its own.
    for (int j = SCATTERED_I0; j <= SCATTERED_I1; ++j) {
        for (int i = SCATTERED_I0; i <= SCATTERED_I1; ++i) {
            CPPUNIT_ASSERT_DOUBLES_EQUAL(overcastOnly.sun(i, j, OVERCAST_K) + 2.0,
                                         both.sun(i, j, OVERCAST_K), SUN_TOLERANCE);
        }
    }

    // Overcast under a gap in the scattered layer is not shadowed by it.
    CPPUNIT_ASSERT_DOUBLES_EQUAL(overcastOnly.sun(5, 5, OVERCAST_K), both.sun(5, 5, OVERCAST_K), SUN_TOLERANCE);
    CPPUNIT_ASSERT(both.sun(2, 2, OVERCAST_K) > both.sun(5, 5, OVERCAST_K));

    // The overcast below does not shadow the scattered layer.
    for (int k = SCATTERED_K0; k <= SCATTERED_K1; ++k) {
        CPPUNIT_ASSERT_DOUBLES_EQUAL(scatteredOnly.sun(2, 2, k), both.sun(2, 2, k), SUN_TOLERANCE);
    }
}

void CloudShadeTests::testScatteredOverOvercastColumnChannels()
{
    Field f(8, 8);
    addOvercast(f);
    addScattered(f);
    f.rebuild(SUN_OVERHEAD);

    // G: overcast under the cell sees the cell above it; in a gap it sees only itself.
    CPPUNIT_ASSERT_DOUBLES_EQUAL(3.0, f.sky(2, 2, OVERCAST_K), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, f.sky(5, 5, OVERCAST_K), TOLERANCE);

    // B: the scattered cell sees the overcast below it, plus the cloud below
    // it within the cell.
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.5, f.ground(2, 2, SCATTERED_K0), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(2.5, f.ground(2, 2, SCATTERED_K1), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, f.ground(2, 2, OVERCAST_K), TOLERANCE);

    // A: each layer is its own run, so has its own base-to-top profile.
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, f.height_(2, 2, OVERCAST_K), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, f.height_(5, 5, OVERCAST_K), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.25, f.height_(2, 2, SCATTERED_K0), TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.75, f.height_(2, 2, SCATTERED_K1), TOLERANCE);
}

void CloudShadeTests::testScatteredOverOvercastObliqueSun()
{
    // A 12 x 12 x 8 field: overcast at k = 1, and a scattered strip one voxel
    // wide (i = 6) and deep (k = 4), running the length of the field in j.
    const int width = 12, height = 8, stripI = 6, stripK = 4, j = 5;
    Field overcastOnly(width, height), both(width, height);
    for (Field* f : {&overcastOnly, &both}) {
        f->setBlock(0, width - 1, 0, width - 1, OVERCAST_K, OVERCAST_K, 1.0f);
    }
    both.setBlock(stripI, stripI, 1, width - 2, stripK, stripK, 1.0f);

    // Sun at 45 degrees elevation towards +x in voxel space.  The field is
    // wider than it is tall in voxels, which runShadeRebuild() compensates for
    // by scaling Z, so the Z component is pre-scaled by height/width here.
    const osg::Vec3f sunOblique(1.0f, 0.0f, float(height) / float(width));
    overcastOnly.rebuild(sunOblique);
    both.rebuild(sunOblique);

    // The shadow lands (stripK - OVERCAST_K) = 3 voxels away from the sun,
    // not directly beneath the strip.
    const int shadowI = stripI - (stripK - OVERCAST_K);
    const float shadowed = both.sun(shadowI, j, OVERCAST_K) - overcastOnly.sun(shadowI, j, OVERCAST_K);
    const float beneath = both.sun(stripI, j, OVERCAST_K) - overcastOnly.sun(stripI, j, OVERCAST_K);
    CPPUNIT_ASSERT(shadowed > 0.5f);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, beneath, SUN_TOLERANCE);

    // With the sun overhead instead, the same strip shadows only the overcast
    // directly beneath it.
    overcastOnly.rebuild(SUN_OVERHEAD);
    both.rebuild(SUN_OVERHEAD);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, both.sun(stripI, j, OVERCAST_K) - overcastOnly.sun(stripI, j, OVERCAST_K), SUN_TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, both.sun(shadowI, j, OVERCAST_K) - overcastOnly.sun(shadowI, j, OVERCAST_K), SUN_TOLERANCE);
}
