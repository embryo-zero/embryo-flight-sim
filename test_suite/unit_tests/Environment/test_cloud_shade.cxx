// test_cloud_shade.cxx - unit tests for the voxel cloud shade texture.
//
// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Stuart Buchanan

#include "config.h"

#include "test_cloud_shade.hxx"

#include <cstring>
#include <vector>

#include <Environment/fgclouds.hxx>
#include <simgear/scene/sky/newcloud.hxx>

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
        std::memcpy(shade.data(), result.detailedShadeData->data(), shade.size() * sizeof(float));
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

    // Sun at 45 degrees elevation towards +x.  The field is not a cube (12 x 12
    // x 8 voxels), but its voxels are, so the shadow must follow the metric sun
    // direction.
    const osg::Vec3f sunOblique(1.0f, 0.0f, 1.0f);
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

namespace {

// A cloud that fills a fixed block of detailed-field voxels, regardless of
// where it is placed.
class BlockCloud : public SGVoxelCloud
{
public:
    BlockCloud(int i0, int i1, int k0, int k1)
        : _i0(i0),
          _i1(i1),
          _k0(k0),
          _k1(k1)
    {
    }

    int addCloudToDetailedVoxelField(ImageRef voxelField, float, osg::Vec3f) const override
    {
        const int width = voxelField->s();
        float* raw = reinterpret_cast<float*>(voxelField->data());
        for (int k = _k0; k <= _k1; ++k) {
            for (int j = _i0; j <= _i1; ++j) {
                for (int i = _i0; i <= _i1; ++i) {
                    float* v = raw + (((size_t)k * width + j) * width + i) * 4;
                    v[0] = 1.0f; // dimension
                    v[1] = 1.0f; // type
                    v[2] = 1.0f; // density
                }
            }
        }
        return _k1;
    }

    int addCloudToRoughVoxelField(ImageRef, float, osg::Vec3f) const override { return 0; }

private:
    int _i0, _i1, _k0, _k1;
};

const int REBUILD_WIDTH = 16, REBUILD_HEIGHT = 8;

FGClouds::RebuildSnapshot makeRebuildSnapshot(const osg::Vec3f& sunDir)
{
    FGClouds::RebuildSnapshot snap;
    snap.detailedFieldWidth = REBUILD_WIDTH;
    snap.detailedFieldHeight = REBUILD_HEIGHT;
    snap.detailedFieldVoxelSize = 200;
    snap.roughFieldWidth = 1;
    snap.roughFieldHeight = 1;
    snap.roughFieldVoxelSize = 1;
    snap.roughVoxelSizeFactor = 1;
    snap.voxelOpticalDepth = 1.0f;
    snap.sdfMaxDistanceVoxels = 8.0f;
    snap.fieldRepeating = true;
    snap.cloudbaseM = 0.0f;
    snap.sunDirVoxel = sunDir;
    return snap;
}

// Shade-only rebuild of a field rebuild's detailed voxel data.
osg::ref_ptr<osg::Image> shadeOnly(const FGClouds::RebuildResult& r, const osg::Vec3f& sunDir)
{
    FGClouds::ShadeSnapshot snap;
    const float* raw = reinterpret_cast<const float*>(r.detailedVoxelData->data());
    snap.width = r.detailedVoxelData->s();
    snap.height = r.detailedVoxelData->r();
    snap.voxelData.assign(raw, raw + (size_t)snap.width * snap.width * snap.height * 4);
    snap.voxelOpticalDepth = 1.0f;
    snap.sunDirVoxel = sunDir;
    return FGClouds::runShadeRebuild(snap).detailedShadeData;
}

bool sameImageData(const osg::Image* a, const osg::Image* b)
{
    return a->s() == b->s() && a->t() == b->t() && a->r() == b->r() &&
           std::memcmp(a->data(), b->data(), a->getTotalSizeInBytes()) == 0;
}

float sunAt(const osg::Image* shade, int i, int j, int k)
{
    const float* raw = reinterpret_cast<const float*>(shade->data());
    return raw[(((size_t)k * shade->s() + j) * shade->s() + i) * 4];
}

} // namespace

void CloudShadeTests::testFieldRebuildIncludesShade()
{
    // A wide lower cloud with a narrower one above part of it.
    const BlockCloud lower(2, 12, 1, 1), upper(4, 6, 4, 5);
    auto snap = makeRebuildSnapshot(SUN_OVERHEAD);
    snap.detailedFieldList = {{&lower, osg::Vec3f()}, {&upper, osg::Vec3f()}};

    const FGClouds::RebuildResult r = FGClouds::runRebuild(snap);

    CPPUNIT_ASSERT(r.detailedShadeData.valid());
    CPPUNIT_ASSERT(sameImageData(r.detailedShadeData.get(), shadeOnly(r, SUN_OVERHEAD).get()));

    // Lower cloud is shadowed by the upper one (2 voxels deep) only beneath it.
    CPPUNIT_ASSERT_DOUBLES_EQUAL(sunAt(r.detailedShadeData.get(), 10, 10, 1) + 2.0,
                                 sunAt(r.detailedShadeData.get(), 5, 5, 1), SUN_TOLERANCE);
}

void CloudShadeTests::testFieldRebuildShadeUsesSnapshotSun()
{
    const BlockCloud lower(2, 12, 1, 1), upper(4, 6, 4, 5);
    const osg::Vec3f sunOblique(1.0f, 0.0f, 1.0f);

    auto overheadSnap = makeRebuildSnapshot(SUN_OVERHEAD);
    auto obliqueSnap = makeRebuildSnapshot(sunOblique);
    overheadSnap.detailedFieldList = obliqueSnap.detailedFieldList = {{&lower, osg::Vec3f()}, {&upper, osg::Vec3f()}};

    const FGClouds::RebuildResult overhead = FGClouds::runRebuild(overheadSnap);
    const FGClouds::RebuildResult oblique = FGClouds::runRebuild(obliqueSnap);

    CPPUNIT_ASSERT(sameImageData(oblique.detailedShadeData.get(), shadeOnly(oblique, sunOblique).get()));
    CPPUNIT_ASSERT(!sameImageData(oblique.detailedShadeData.get(), overhead.detailedShadeData.get()));
}

void CloudShadeTests::testEmptyFieldRebuildIncludesShade()
{
    const FGClouds::RebuildResult r = FGClouds::runRebuild(makeRebuildSnapshot(SUN_OVERHEAD));

    CPPUNIT_ASSERT(r.detailedShadeData.valid());
    CPPUNIT_ASSERT_EQUAL(1, r.detailedShadeData->s());
    const float* shade = reinterpret_cast<const float*>(r.detailedShadeData->data());
    for (int c = 0; c < 4; ++c) {
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, shade[c], TOLERANCE);
    }
}

void CloudShadeTests::testScatteredOverOvercastLowSun()
{
    // Sun at atan(1/3) = 18.4 degrees elevation: a strip 3 voxels above the
    // overcast casts its shadow 9 voxels away, in a 16 x 16 x 8 field.
    const int width = 16, height = 8, stripI = 12, stripK = 4, j = 8;
    Field overcastOnly(width, height), both(width, height);
    for (Field* f : {&overcastOnly, &both}) {
        f->setBlock(0, width - 1, 0, width - 1, OVERCAST_K, OVERCAST_K, 1.0f);
    }
    both.setBlock(stripI, stripI, 1, width - 2, stripK, stripK, 1.0f);

    const osg::Vec3f sunLow(3.0f, 0.0f, 1.0f);
    overcastOnly.rebuild(sunLow);
    both.rebuild(sunLow);

    auto shadowAt = [&](int i) { return both.sun(i, j, OVERCAST_K) - overcastOnly.sun(i, j, OVERCAST_K); };
    const int shadowI = stripI - 3 * (stripK - OVERCAST_K);

    // The path through the strip at this angle is longer than at 45 degrees.
    CPPUNIT_ASSERT(shadowAt(shadowI) > 0.8f);

    // Nothing directly beneath the strip, nor where a sun at 4x the true
    // elevation tangent would put the shadow (under a quarter of the distance).
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, shadowAt(stripI), SUN_TOLERANCE);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, shadowAt(stripI - 2), SUN_TOLERANCE);
}

namespace {

// A cloud that fills a fixed block of rough-field voxels, and nothing in the
// detailed field.
class RoughBlockCloud : public SGVoxelCloud
{
public:
    RoughBlockCloud(int i0, int i1, int j0, int j1, int k0, int k1)
        : _i0(i0),
          _i1(i1),
          _j0(j0),
          _j1(j1),
          _k0(k0),
          _k1(k1)
    {
    }

    int addCloudToDetailedVoxelField(ImageRef, float, osg::Vec3f) const override { return 0; }

    int addCloudToRoughVoxelField(ImageRef voxelField, float, osg::Vec3f) const override
    {
        const int width = voxelField->s();
        float* raw = reinterpret_cast<float*>(voxelField->data());
        for (int k = _k0; k <= _k1; ++k)
            for (int j = _j0; j <= _j1; ++j)
                for (int i = _i0; i <= _i1; ++i)
                    raw[(((size_t)k * width + j) * width + i) * 4 + 2] = 1.0f;
        return _k1;
    }

private:
    int _i0, _i1, _j0, _j1, _k0, _k1;
};

// A 16 x 16 x 8 detailed field inside a 32 x 32 x 4 rough field of voxels twice
// the size, so the rough field covers 4 times the detailed field's width.
FGClouds::RebuildSnapshot makeNonRepeatingSnapshot(const osg::Vec3f& sunDir)
{
    auto snap = makeRebuildSnapshot(sunDir);
    snap.fieldRepeating = false;
    snap.roughVoxelSizeFactor = 2;
    snap.roughFieldWidth = 32;
    snap.roughFieldHeight = 4;
    snap.roughFieldVoxelSize = 400;
    return snap;
}

float roughSunAt(const FGClouds::RebuildResult& r, int i, int j, int k)
{
    const float* raw = reinterpret_cast<const float*>(r.roughShadeData->data());
    return raw[(((size_t)k * r.roughShadeData->s() + j) * r.roughShadeData->s() + i) * 4];
}

} // namespace

void CloudShadeTests::testRepeatingFieldHasEmptyRoughShade()
{
    const BlockCloud cloud(4, 6, 1, 2);
    auto snap = makeRebuildSnapshot(SUN_OVERHEAD);
    snap.detailedFieldList = {{&cloud, osg::Vec3f()}};

    const FGClouds::RebuildResult r = FGClouds::runRebuild(snap);

    CPPUNIT_ASSERT(r.roughShadeData.valid());
    CPPUNIT_ASSERT_EQUAL(1, r.roughShadeData->s());
    const float* shade = reinterpret_cast<const float*>(r.roughShadeData->data());
    for (int c = 0; c < 4; ++c) {
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, shade[c], TOLERANCE);
    }
}

void CloudShadeTests::testRoughShadeOpticalDepthScale()
{
    // One rough voxel deep, so 2 detailed voxels' worth of cloud.
    const RoughBlockCloud cloud(4, 6, 4, 6, 1, 1);
    auto snap = makeNonRepeatingSnapshot(SUN_OVERHEAD);
    snap.roughFieldList = {{&cloud, osg::Vec3f()}};

    const FGClouds::RebuildResult r = FGClouds::runRebuild(snap);

    CPPUNIT_ASSERT_EQUAL(32, r.roughShadeData->s());
    CPPUNIT_ASSERT_DOUBLES_EQUAL(2.0, roughSunAt(r, 5, 5, 0), SUN_TOLERANCE);
}

void CloudShadeTests::testRoughCloudShadowsDetailedField()
{
    // The detailed field spans rough voxels 12..19.  A rough-only cloud at rough
    // i = 21..25, k = 1 sits beyond its +x side, about 1.5 detailed voxels above
    // detailed k = 1 and 8.5 detailed voxels beyond detailed i = 14: a sun at
    // 10 degrees towards +x passes through it from there.  A cloud the same
    // distance beyond the -x side is on the wrong side to shadow anything.
    const RoughBlockCloud sunSide(21, 25, 8, 23, 1, 1), farSide(6, 10, 8, 23, 1, 1);
    const osg::Vec3f sunLow(8.5f, 0.0f, 1.5f);

    auto withShadow = makeNonRepeatingSnapshot(sunLow);
    withShadow.roughFieldList = {{&sunSide, osg::Vec3f()}};
    auto without = makeNonRepeatingSnapshot(sunLow);
    without.roughFieldList = {{&farSide, osg::Vec3f()}};

    const FGClouds::RebuildResult shadowed = FGClouds::runRebuild(withShadow);
    const FGClouds::RebuildResult clear = FGClouds::runRebuild(without);

    // The detailed field itself is empty in both, so any sun optical depth in it
    // comes from the rough field.
    CPPUNIT_ASSERT(sunAt(shadowed.detailedShadeData.get(), 14, 8, 1) > 0.5f);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, sunAt(clear.detailedShadeData.get(), 14, 8, 1), SUN_TOLERANCE);
}

void CloudShadeTests::testShadeRebuildIncludesRoughField()
{
    const RoughBlockCloud roughCloud(21, 25, 8, 23, 1, 1);
    const BlockCloud detailedCloud(4, 6, 1, 2);
    const osg::Vec3f sunLow(8.5f, 0.0f, 1.5f);
    auto snap = makeNonRepeatingSnapshot(sunLow);
    snap.roughFieldList = {{&roughCloud, osg::Vec3f()}};
    snap.detailedFieldList = {{&detailedCloud, osg::Vec3f()}};

    const FGClouds::RebuildResult r = FGClouds::runRebuild(snap);

    FGClouds::ShadeSnapshot shadeSnap;
    const float* raw = reinterpret_cast<const float*>(r.detailedVoxelData->data());
    shadeSnap.width = r.detailedVoxelData->s();
    shadeSnap.height = r.detailedVoxelData->r();
    shadeSnap.voxelData.assign(raw, raw + (size_t)shadeSnap.width * shadeSnap.width * shadeSnap.height * 4);
    const float* roughRaw = reinterpret_cast<const float*>(r.roughVoxelData->data());
    shadeSnap.roughWidth = r.roughVoxelData->s();
    shadeSnap.roughHeight = r.roughVoxelData->r();
    shadeSnap.roughVoxelSizeFactor = r.roughVoxelSizeFactor;
    shadeSnap.roughVoxelData.assign(roughRaw, roughRaw + (size_t)shadeSnap.roughWidth * shadeSnap.roughWidth * shadeSnap.roughHeight * 4);
    shadeSnap.voxelOpticalDepth = 1.0f;
    shadeSnap.sunDirVoxel = sunLow;

    const FGClouds::ShadeRebuildResult s = FGClouds::runShadeRebuild(shadeSnap);

    CPPUNIT_ASSERT(sameImageData(r.detailedShadeData.get(), s.detailedShadeData.get()));
    CPPUNIT_ASSERT(sameImageData(r.roughShadeData.get(), s.roughShadeData.get()));
}

void CloudShadeTests::testSunMarchStopsWhenSaturated()
{
    // A 6-voxel-deep column (k = 1..6) with optical depth 1 per voxel.
    Field f(4, 8);
    f.setBlock(1, 1, 1, 1, 1, 6, 1.0f);
    f.rebuild(SUN_OVERHEAD);

    // From k = 3 the ray sees 3.5 (the rest of the column plus the half-voxel
    // fade at its top) and never saturates.
    CPPUNIT_ASSERT_DOUBLES_EQUAL(3.5, f.sun(1, 1, 3), SUN_TOLERANCE);

    // From k = 0 it would see 6.0, but stops within one step of passing 5.
    CPPUNIT_ASSERT(f.sun(1, 1, 0) > 5.0f);
    CPPUNIT_ASSERT(f.sun(1, 1, 0) <= 5.5f);
}

void CloudShadeTests::testSaturatedRayIgnoresRoughField()
{
    // As in testRoughCloudShadowsDetailedField (the ray from detailed i = 12
    // also passes through the rough cloud), but with a dense detailed cloud
    // right next to it on the sun side, which saturates the ray well before
    // the detailed field's edge.
    const RoughBlockCloud sunSide(21, 25, 8, 23, 1, 1);
    const BlockCloud detailedCloud(13, 13, 1, 1);
    const osg::Vec3f sunLow(8.5f, 0.0f, 1.5f);
    auto snap = makeNonRepeatingSnapshot(sunLow);
    snap.voxelOpticalDepth = 10.0f;
    snap.roughFieldList = {{&sunSide, osg::Vec3f()}};
    snap.detailedFieldList = {{&detailedCloud, osg::Vec3f()}};

    const FGClouds::RebuildResult r = FGClouds::runRebuild(snap);

    // Saturated, and by no more than one step (half a voxel at 10 per voxel).
    const float sun = sunAt(r.detailedShadeData.get(), 12, 13, 1);
    CPPUNIT_ASSERT(sun > 5.0f);
    CPPUNIT_ASSERT(sun <= 10.0f);
}
