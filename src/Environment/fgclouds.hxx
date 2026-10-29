// Build a cloud layer based on metar
//
// Written by Harald JOHNSEN, started April 2005.
//
// SPDX-FileCopyrightText: 2005 Harald JOHNSEN <hjohnsen@evc.net>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <osg/Group>
#include <osg/Image>
#include <osg/NodeCallback>
#include <string>
#include <vector>

#include <atomic>
#include <functional>
#include <future>
#include <mutex>

#include <Environment/environment.hxx>
#include <simgear/props/propertyObject.hxx>
#include <simgear/scene/util/SGReaderWriterOptions.hxx>

using std::vector;

// forward decls
class SGPropertyNode;
class SGCloudField;
class SGVoxelCloud;


class FGClouds
{
    friend class CloudLayerTests;

public:
    // Snapshot of everything the background thread needs - captured on the
    // main thread before the async task is launched.
    struct RebuildSnapshot {
        // Copy of placement map.  The raw pointers are kept valid by clouds,
        // below, even if the clouds are removed from _cloudPlacementMap while
        // the task runs.
        using LocalPlacement = std::pair<const SGVoxelCloud*, osg::Vec3f>;
        std::vector<LocalPlacement> detailedFieldList;
        std::vector<LocalPlacement> roughFieldList;
        std::vector<std::shared_ptr<const SGVoxelCloud>> clouds;

        // Config captured at snapshot time
        int detailedFieldWidth{};
        int detailedFieldHeight{};
        int detailedFieldVoxelSize{};
        int roughFieldWidth{};
        int roughFieldHeight{};
        int roughFieldVoxelSize{};
        int roughVoxelSizeFactor{};
        float voxelOpticalDepth{};
        float sdfMaxDistanceVoxels{};
        bool fieldRepeating{};

        // Field center for fgSet* calls at commit time
        SGVec3<double> centerCart;
        osg::Matrixf cloudPosMatrix;
        float cloudbaseM = 999999.0f;

        // Sun direction (Z-up, in the frame of the field being built), for the
        // shade texture built alongside the field.
        osg::Vec3f sunDirVoxel;
    };

    // Output bundle produced by the background thread
    struct RebuildResult {
        osg::ref_ptr<osg::Image> detailedVoxelData;
        osg::ref_ptr<osg::Image> roughVoxelData;
        osg::ref_ptr<osg::Image> windOffsetData;
        double maxZM = 0.0f;
        float cloudbaseM = 999999.0f;
        bool fieldRepeating = true;

        // Field center this result was built for - applied to the shader-facing
        // cloud-center-* uniforms at commit time, in lockstep with the voxel data
        // itself (see the comment on RebuildSnapshot::centerCart).
        SGVec3d centerCart;

        // Shade textures for detailedVoxelData and roughVoxelData, committed
        // together with them so a field is never drawn with another field's shading.
        osg::ref_ptr<osg::Image> detailedShadeData;
        osg::ref_ptr<osg::Image> roughShadeData;
        osg::Vec3f sunDirVoxel;
        int roughVoxelSizeFactor = 1;
    };

    // Snapshot needed to (re)build just the shade texture, independently of
    // the rest of the voxel field.  This is cheap enough to redo whenever the
    // sun direction has moved significantly, without re-running the full
    // cloud placement/voxel field rebuild.
    struct ShadeSnapshot {
        // A private copy of the detailed voxel field's raw RGBA float data, taken on
        // the main thread.  We deliberately copy rather than share the osg::Image with
        // the background shade-rebuild thread: that Image is also live in an
        // osg::Texture3D and can be touched concurrently by the render/GPU-upload
        // path, which is a data race even though osg::Referenced ref-counting itself
        // is atomic.
        std::vector<float> voxelData;
        int width{};
        int height{};
        float voxelOpticalDepth{};

        // Copy of the rough voxel field, in the same layout; empty when the field
        // is repeating and so has no rough field.
        std::vector<float> roughVoxelData;
        int roughWidth{};
        int roughHeight{};
        int roughVoxelSizeFactor{1};

        // Sun direction (world-space, already transformed to Z-up)
        osg::Vec3f sunDirVoxel;

        // The committed field this snapshot was taken from.
        unsigned int fieldGeneration{};
    };

    struct ShadeRebuildResult {
        osg::ref_ptr<osg::Image> detailedShadeData;
        osg::ref_ptr<osg::Image> roughShadeData;
        unsigned int fieldGeneration{};
    };

private:
    // Protects _cloudPlacementMap from concurrent access between
    // addCloud/removeCloud (main thread) and snapshot capture.
    mutable std::mutex _placementMutex;

    // Set on any mutation; cleared once a new async rebuild is launched.
    std::atomic<bool> _fieldDirty{false};

    // The running or completed async task.  checked in updateFromOsgTraversal.
    std::future<RebuildResult> _rebuildFuture;

    // The running or completed async shade-only rebuild task.
    std::future<ShadeRebuildResult> _shadeRebuildFuture;

    // Set to true by the background thread when it finishes.
    std::atomic<bool> _rebuildComplete{false};

    // Sun direction (Z-up, voxel space) used for the most recently launched
    // shade build, so we can detect when it has drifted enough to need redoing.
    osg::Vec3f _lastShadeSunDir;

    // Incremented on every field commit.  A shade-only rebuild result is
    // discarded if the field has been replaced since its snapshot was taken.
    unsigned int _fieldGeneration{0};

    // Whether the committed field has a rough field, and its voxel size factor,
    // for re-shading it when the sun moves.
    bool _committedRoughField{false};
    int _committedRoughVoxelSizeFactor{1};

    size_t _roughFieldWidth;
    size_t _roughFieldHeight;
    size_t _roughFieldVoxelSize;
    size_t _detailedFieldWidth;
    size_t _detailedFieldHeight;
    size_t _detailedFieldVoxelSize;

    // Shared with any in-flight rebuild's snapshot.
    typedef std::pair<std::shared_ptr<const SGVoxelCloud>, osg::Vec3f> CloudPlacement;

    typedef std::unordered_map<int, CloudPlacement> CloudPlacementMap;

    double buildCloud(SGPropertyNode* cloud_def_root, SGPropertyNode* box_def_root,
                      const std::string& name, double altM, double grid_z_rand, SGCloudField* layer);
    void buildLayer(int iLayer, const std::string& name, double coverage, double altM);

    void buildCloudLayers(void);

    int update_event;

    int index;

    CloudPlacementMap _cloudPlacementMap;

    // This is the ECF cartesian coordinates of the voxel field.
    SGVec3d _centerCart;
    osg::Matrixd _cloudPosMatrix;

    // Whether the cloud field is repeating (by simply mirroring the voxel space texture)
    bool _fieldRepeating;

    osg::ref_ptr<simgear::SGReaderWriterOptions> _options;

    // Properties that are subsequently mapped to Uniforms by the Effects system
    SGPropObjDouble _cloudBaseM;
    SGPropObjDouble _cloudBaseZNorm;
    SGPropObjDouble _cloudCenterX;
    SGPropObjDouble _cloudCenterY;
    SGPropObjDouble _cloudCenterZ;
    SGPropObjBool _mirrorU;
    SGPropObjBool _mirrorV;
    SGPropObjBool _cloudFieldRepeating;
    SGPropObjDouble _activeVoxelFieldHeightNorm;

    // Minimum sun direction change (in degrees) that triggers a shade rebuild.
    SGPropObjDouble _shadeUpdateAngleDeg;

    // Fraction of the way from the detailed field's center to its own edge that the camera may
    // drift (when non-repeating) before a full field rebuild/recenter is triggered.
    SGPropObjDouble _recenterMarginNorm;

    // A node in the scenegraph purely used to ensure that the voxel data
    // is modified during the update traversal.
    osg::ref_ptr<osg::Group> _cloudUpdateNode;

    // The voxel images.  These are kept in FGClouds to ensure they aren't
    // destroyed by OSG
    osg::ref_ptr<osg::Image> _detailedVoxelData;
    osg::ref_ptr<osg::Image> _roughVoxelData;
    osg::ref_ptr<osg::Image> _detailedShadeData;
    osg::ref_ptr<osg::Image> _roughShadeData;
    osg::ref_ptr<osg::Image> _windOffsetData;

    // Command functions.
    bool add3DCloud(const SGPropertyNode* arg, SGPropertyNode* root);
    bool delete3DCloud(const SGPropertyNode* arg, SGPropertyNode* root);
    bool move3DCloud(const SGPropertyNode* arg, SGPropertyNode* root);
    bool setCloudsRepeating(const SGPropertyNode* arg, SGPropertyNode* root);

    /**
     * Add a new cloud with a given index at a specific point defined by lon/lat and an x/y offset
     */
    bool addCloud(std::unique_ptr<const SGVoxelCloud> cloud, int index, float lon, float lat, float alt, float x, float y);
    bool addCloud(std::unique_ptr<const SGVoxelCloud> cloud, int index, SGGeod loc, float x, float y);
    bool addCloud(std::unique_ptr<const SGVoxelCloud> cloud, int index, float lon, float lat, float alt);
    bool addCloud(std::unique_ptr<const SGVoxelCloud> cloud, int index, SGGeod loc);

    // add one cloud, data is not copied, ownership given
    void addCloud(SGVec3f& pos, const SGVoxelCloud* cloud);

    // Cloud handling functions.
    bool removeCloud(int index);
    bool repositionCloud(int index, float lon, float lat, float alt);
    bool repositionCloud(int index, float lon, float lat, float alt, float x, float y);

    // Utility functions
    static void generateSDF(osg::ref_ptr<osg::Image>, float maxDistanceVoxels);
    osg::Vec3f getFinalPos(SGGeod loc, float x, float y);

    // Sun direction in voxel (Z-up, field-relative) space. Shared
    // by both the full rebuild snapshot and the shade-only snapshot.
    osg::Vec3f computeSunDirVoxel() const;

    // Asynchronous rebuild of the cloud layers, including their shade texture.
    RebuildSnapshot captureSnapshot();
    void commitResult(RebuildResult result);

    // Asynchronous rebuild of just the shade texture for the committed field,
    // re-run whenever the sun direction changes significantly.
    ShadeSnapshot captureShadeSnapshot();
    void commitShadeResult(ShadeRebuildResult result);

    // Shade texture for a voxel field (RGBA float, width x width x height).  If
    // given, beyondField(px, py, pz) returns the sun optical depth from a point
    // where a sun ray leaves the field sideways (in this field's voxel coordinates)
    // onward to the sun, and is zero at and above height beyondTopZ.
    static osg::ref_ptr<osg::Image> buildShadeImage(const float* voxelRaw, int width, int height,
                                                    float voxelOpticalDepth, const osg::Vec3f& sunDirVoxel,
                                                    const std::function<float(float, float, float)>& beyondField = {},
                                                    float beyondTopZ = 0.0f);

    // Shade textures for a detailed field and, if roughRaw is given, the rough
    // field around it.  The rough field is shaded first, so that detailed sun
    // rays leaving the detailed field carry on through it.
    static void buildFieldShade(const float* detailedRaw, int detailedWidth, int detailedHeight,
                                const float* roughRaw, int roughWidth, int roughHeight, int roughVoxelSizeFactor,
                                float voxelOpticalDepth, const osg::Vec3f& sunDirVoxel,
                                osg::ref_ptr<osg::Image>& detailedShade, osg::ref_ptr<osg::Image>& roughShade);

public:
    FGClouds();
    ~FGClouds();

    void Init(void);

    int get_update_event(void) const;
    void set_update_event(int count);
    bool get_3dClouds() const;
    void set_3dClouds(bool enable);

    bool isDirty(void) const { return _fieldDirty; }
    void setDirty(bool dirty) { _fieldDirty = dirty; }

    bool isCloudsRepeating(void) const { return _fieldRepeating; }
    void setCloudsRepeating(bool repeat) { _fieldRepeating = repeat; }

    void updateWindColumn(double dt, FGEnvironment* env);
    void updateFromOsgTraversal();
    void updateRepeatingField();
    void updateNonRepeatingField();
    osg::ref_ptr<osg::Group> getCloudUpdateNode() { return _cloudUpdateNode; }
    static RebuildResult runRebuild(RebuildSnapshot snap);         // static = no 'this' access
    static ShadeRebuildResult runShadeRebuild(ShadeSnapshot snap); // static = no 'this' access
    static void computeColumnShadeChannels(const float* voxelRaw, float* shadeRaw,
                                           int width, int height, float voxelOpticalDepth);
};

class FGCloudUpdateCallback : public osg::NodeCallback
{
public:
    FGCloudUpdateCallback(FGClouds* clouds)
        : _clouds(clouds) {}

    void operator()(osg::Node* node, osg::NodeVisitor* nv) override
    {
        if (nv->getVisitorType() == osg::NodeVisitor::UPDATE_VISITOR) {
            _clouds->updateFromOsgTraversal();
        }
        traverse(node, nv);
    }

private:
    FGClouds* _clouds;
};
