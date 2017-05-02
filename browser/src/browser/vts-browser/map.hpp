#ifndef MAP_H_cvukikljqwdf
#define MAP_H_cvukikljqwdf

#include <string>
#include <memory>
#include <queue>
#include <atomic>
#include <unordered_map>
#include <unordered_set>

#include <boost/thread/mutex.hpp>
#include <boost/optional/optional_io.hpp>
#include <vts-libs/vts/urltemplate.hpp>
#include <vts-libs/vts/nodeinfo.hpp>
#include <dbglog/dbglog.hpp>
#include <utility/uri.hpp>

#include <vts/statistics.hpp>
#include <vts/options.hpp>
#include <vts/rendering.hpp>
#include <vts/resources.hpp>
#include <vts/buffer.hpp>
#include <vts/math.hpp>

#include "mapConfig.hpp"
#include "resource.hpp"
#include "resourceMap.hpp"

namespace vts
{

using vtslibs::vts::NodeInfo;
using vtslibs::vts::TileId;
using vtslibs::vts::MetaNode;
using vtslibs::vts::UrlTemplate;

enum class Validity
{
    Indeterminate,
    Invalid,
    Valid,
};

class BoundParamInfo : public vtslibs::registry::View::BoundLayerParams
{
public:
    typedef std::vector<BoundParamInfo> list;

    BoundParamInfo(const vtslibs::registry::View::BoundLayerParams &params);
    const mat3f uvMatrix() const;
    Validity prepare(const NodeInfo &nodeInfo, class MapImpl *impl,
                 uint32 subMeshIndex);
    bool operator < (const BoundParamInfo &rhs) const;
    
    UrlTemplate::Vars orig;
    UrlTemplate::Vars vars;
    BoundInfo *bound;
    int depth;
    bool watertight;
    bool transparent;
};

class RenderTask
{
public:
    std::shared_ptr<MeshAggregate> meshAgg;
    std::shared_ptr<GpuMesh> mesh;
    std::shared_ptr<GpuTexture> textureColor;
    std::shared_ptr<GpuTexture> textureMask;
    mat4 model;
    mat3f uvm;
    vec4f color;
    bool externalUv;
    bool transparent;
    
    RenderTask();
    bool ready() const;
};

class TraverseNode
{
public:
    NodeInfo nodeInfo;
    vec3 cornersPhys[8];
    vec3 aabbPhys[2];
    vec3 surrogatePhys;
    std::vector<std::shared_ptr<RenderTask>> draws;
    std::vector<std::shared_ptr<TraverseNode>> childs;
    const SurfaceStackItem *surface;
    uint32 lastAccessTime;
    uint32 flags;
    float texelSize;
    float surrogateValue;
    uint16 displaySize;
    Validity validity;
    bool empty;
    
    TraverseNode(const NodeInfo &nodeInfo);
    ~TraverseNode();
    void clear();
    bool ready() const;
};

class MapImpl
{
public:
    MapImpl(class MapFoundation *mapFoundation,
            const class MapFoundationOptions &options);

    class MapFoundation *const mapFoundation;
    std::shared_ptr<MapConfig> mapConfig;
    std::string mapConfigPath;
    std::string mapConfigView;
    MapStatistics statistics;
    MapOptions options;
    DrawBatch draws;
    bool initialized;
    
    class Navigation
    {
    public:
        vec3 inertiaMotion;
        vec3 inertiaRotation;
        double inertiaViewExtent;
        std::queue<std::shared_ptr<class HeightRequest>> panZQueue;
        boost::optional<double> lastPanZShift;
        
        Navigation();
    } navigation;
    
    class Resources
    {
    public:
        static const std::string invalidUrlFileName;
        
        std::unordered_map<std::string, std::shared_ptr<Resource>> resources;
        std::unordered_set<ResourceImpl*> prepareQueLocked;
        std::unordered_set<ResourceImpl*> prepareQueNoLock;
        std::unordered_set<std::string> invalidUrlLocked;
        std::unordered_set<std::string> invalidUrlNoLock;
        std::string cachePath;
        boost::mutex mutPrepareQue;
        boost::mutex mutInvalidUrls;
        std::atomic_uint downloads;
        Fetcher *fetcher;
        bool destroyTheFetcher;
        
        Resources(const std::string &cachePath, bool keepInvalidUrls);
        ~Resources();
    } resources;
    
    class Renderer
    {
    public:
        std::shared_ptr<TraverseNode> traverseRoot;
        std::queue<std::shared_ptr<TraverseNode>> traverseQueue;
        mat4 viewProj;
        mat4 viewProjRender;
        vec4 frustumPlanes[6];
        vec3 perpendicularUnitVector;
        vec3 forwardUnitVector;
        vec3 cameraPosPhys;
        uint32 windowWidth;
        uint32 windowHeight;
        uint32 metaTileBinaryOrder;
        
        Renderer();
    } renderer;
    
    // map foundation methods
    void setMapConfigPath(const std::string &mapConfigPath);
    void purgeHard();
    void purgeSoft();
    void printDebugInfo();
    
    // navigation
    void pan(const vec3 &value);
    void rotate(const vec3 &value);
    void checkPanZQueue();
    const std::pair<vtslibs::vts::NodeInfo, vec2> findInfoNavRoot(
            const vec2 &navPos);
    const NodeInfo findInfoSdsSampled(const NodeInfo &info,
                                      const vec2 &sdsPos);
    void resetPositionAltitude(double resetOffset);
    void convertPositionSubjObj();
    void positionToPhys(vec3 &center, vec3 &dir, vec3 &up);
    double positionObjectiveDistance();
    
    // resources methods
    void dataInitialize(Fetcher *fetcher);
    void dataFinalize();
    bool dataTick();
    void dataRenderInitialize();
    void dataRenderFinalize();
    bool dataRenderTick();
    void loadResource(ResourceImpl *r);
    void fetchedFile(FetchTask *task);
    void touchResource(std::shared_ptr<Resource> resource, double priority = 0);
    std::shared_ptr<GpuTexture> getTexture(const std::string &name);
    std::shared_ptr<GpuMesh> getMeshRenderable(
            const std::string &name);
    std::shared_ptr<MapConfig> getMapConfig(const std::string &name);
    std::shared_ptr<MetaTile> getMetaTile(const std::string &name);
    std::shared_ptr<NavTile> getNavTile(const std::string &name);
    std::shared_ptr<MeshAggregate> getMeshAggregate(const std::string &name);
    std::shared_ptr<ExternalBoundLayer> getExternalBoundLayer(
            const std::string &name);
    std::shared_ptr<BoundMetaTile> getBoundMetaTile(const std::string &name);
    std::shared_ptr<BoundMaskTile> getBoundMaskTile(const std::string &name);
    Validity getResourceValidity(const std::string &name);
    const std::string convertNameToCache(const std::string &path);
    bool availableInCache(const std::string &name);
    const std::string convertNameToPath(std::string path, bool preserveSlashes);
    
    // renderer methods
    void renderInitialize();
    void renderFinalize();
    void renderTick(uint32 windowWidth, uint32 windowHeight);
    const TileId roundId(TileId nodeId);
    Validity reorderBoundLayers(const NodeInfo &nodeInfo, uint32 subMeshIndex,
                           BoundParamInfo::list &boundList);
    void touchResources(std::shared_ptr<TraverseNode> trav);
    void touchResources(std::shared_ptr<RenderTask> task);
    bool visibilityTest(const TraverseNode *trav);
    bool coarsenessTest(const TraverseNode *trav);
    Validity checkMetaNode(SurfaceInfo *surface, const TileId &nodeId,
                           const MetaNode *&node);
    void renderNode(std::shared_ptr<TraverseNode> &trav);
    void traverseValidNode(std::shared_ptr<TraverseNode> &trav);
    bool traverseDetermineSurface(std::shared_ptr<TraverseNode> &trav);
    bool traverseDetermineBoundLayers(std::shared_ptr<TraverseNode> &trav);
    void traverse(std::shared_ptr<TraverseNode> &trav, bool loadOnly);
    void traverseClearing(std::shared_ptr<TraverseNode> &trav);
    void updateCamera();
    bool prerequisitesCheck();
};

} // namespace vts

#endif