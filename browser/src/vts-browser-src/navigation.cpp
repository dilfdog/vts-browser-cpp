#include "map.hpp"
#include <boost/algorithm/string.hpp>

namespace vts
{

namespace
{

inline void normalizeAngle(double &a)
{
    a = modulo(a, 360);
}

} // namespace

MapImpl::Navigation::Navigation() :
    inertiaMotion(0,0,0), inertiaRotation(0,0,0),
    autoMotion(0,0,0), autoRotation(0,0,0),
    inertiaViewExtent(0), navigationMode(MapOptions::NavigationMode::Azimuthal)
{}

class HeightRequest
{
public:
    struct CornerRequest
    {
        const NodeInfo nodeInfo;
        std::shared_ptr<TraverseNode> trav;
        boost::optional<double> result;
        
        CornerRequest(const NodeInfo &nodeInfo) :
            nodeInfo(nodeInfo)
        {}
        
        Validity process(class MapImpl *map)
        {
            if (result)
                return vtslibs::vts::GeomExtents::validSurrogate(*result)
                        ? Validity::Valid : Validity::Invalid;
            
            if (!trav)
            {
                trav = map->renderer.traverseRoot;
                if (!trav)
                    return Validity::Indeterminate;
            }
            
            // load if needed
            switch (trav->validity)
            {
            case Validity::Invalid:
                return Validity::Invalid;
            case Validity::Indeterminate:
                map->traverse(trav, true);
                return Validity::Indeterminate;
            case Validity::Valid:
                break;
            default:
				LOGTHROW(fatal, std::invalid_argument)
						<< "Invalid resource state";
            }
            
            // check id
            if (trav->nodeInfo.nodeId() == nodeInfo.nodeId()
                    || trav->childs.empty())
            {
                result.emplace(trav->surrogateValue);
                return process(nullptr);
            }
            
            { // find child
                uint32 lodDiff = nodeInfo.nodeId().lod
                            - trav->nodeInfo.nodeId().lod - 1;
                TileId id = nodeInfo.nodeId();
                id.lod -= lodDiff;
                id.x >>= lodDiff;
                id.y >>= lodDiff;
                
                for (auto &&it : trav->childs)
                {
                    if (it->nodeInfo.nodeId() == id)
                    {
                        trav = it;
                        return process(map);
                    }
                }    
            }
            return Validity::Invalid;
        }
    };
    
    std::vector<CornerRequest> corners;
    
    boost::optional<NodeInfo> nodeInfo;
    boost::optional<double> result;
    const vec2 navPos;
    vec2 sds;
    vec2 interpol;
    double resetOffset;
    
    HeightRequest(const vec2 &navPos) : navPos(navPos),
        sds(std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()),
        interpol(std::numeric_limits<double>::quiet_NaN(),
                 std::numeric_limits<double>::quiet_NaN()),
        resetOffset(std::numeric_limits<double>::quiet_NaN())
    {}
    
    Validity process(class MapImpl *map)
    {
        if (result)
            return Validity::Valid;
        
        if (corners.empty())
        {
            // find initial position
            corners.reserve(4);
            auto nisds = map->findInfoNavRoot(navPos);
            sds = nisds.second;
            nodeInfo.emplace(map->findInfoSdsSampled(nisds.first, sds));
            
            // find top-left corner position
            math::Extents2 ext = nodeInfo->extents();
            vec2 center = vecFromUblas<vec2>(ext.ll + ext.ur) * 0.5;
            vec2 size = vecFromUblas<vec2>(ext.ur - ext.ll);
            interpol = sds - center;
            interpol(0) /= size(0);
            interpol(1) /= size(1);
            TileId cornerId = nodeInfo->nodeId();
            if (sds(0) < center(0))
            {
                cornerId.x--;
                interpol(0) += 1;
            }
            if (sds(1) < center(1))
                interpol(1) += 1;
            else
                cornerId.y--;
            
            // prepare all four corners
            for (uint32 i = 0; i < 4; i++)
            {
                TileId nodeId = cornerId;
                nodeId.x += i % 2;
                nodeId.y += i / 2;
                corners.emplace_back(NodeInfo(map->mapConfig->referenceFrame,
                                              nodeId, false, *map->mapConfig));
            }
            
            map->statistics.lastHeightRequestLod = nodeInfo->nodeId().lod;
        }
        
        { // process corners
            assert(corners.size() == 4);
            bool determined = true;
            for (auto &&it : corners)
            {
                switch (it.process(map))
                {
                case Validity::Invalid:
                    return Validity::Invalid;
                case Validity::Indeterminate:
                    determined = false;
                    break;
                case Validity::Valid:
                    break;
                default:
                    assert(false);
                }
            }
            if (!determined)
                return Validity::Indeterminate; // try again later
        }
        
        // find the height
        assert(interpol(0) >= 0 && interpol(0) <= 1);
        assert(interpol(1) >= 0 && interpol(1) <= 1);
        double height = interpolate(
            interpolate(*corners[2].result, *corners[3].result, interpol(0)),
            interpolate(*corners[0].result, *corners[1].result, interpol(0)),
            interpol(1));
        height = map->mapConfig->convertor->convert(vec2to3(sds, height),
            nodeInfo->srs(),
            map->mapConfig->referenceFrame.model.navigationSrs)(2);
        result.emplace(height);
        return Validity::Valid;
    }
};

void MapImpl::checkPanZQueue(double &height)
{
    if (navigation.panZQueue.empty())
        return;
    
    HeightRequest &task = *navigation.panZQueue.front();
    double nh = std::numeric_limits<double>::quiet_NaN();
    switch (task.process(this))
    {
    case Validity::Indeterminate:
        return; // try again later
    case Validity::Invalid:
        navigation.panZQueue.pop();
        return; // request cannot be served
    case Validity::Valid:
        nh = *task.result;
        break;
    default:
        LOGTHROW(fatal, std::invalid_argument) << "Invalid resource state";
    }
    
    // apply the height to the camera
    assert(nh == nh);
    if (task.resetOffset == task.resetOffset)
        height = nh + task.resetOffset;
    else if (navigation.lastPanZShift)
        height += nh - *navigation.lastPanZShift;
    navigation.lastPanZShift.emplace(nh);
    navigation.panZQueue.pop();
}

const std::pair<NodeInfo, vec2> MapImpl::findInfoNavRoot(const vec2 &navPos)
{
    for (auto &&it : mapConfig->referenceFrame.division.nodes)
    {
        if (it.second.partitioning.mode
                != vtslibs::registry::PartitioningMode::bisection)
            continue;
        NodeInfo ni(mapConfig->referenceFrame, it.first,
                    false, *mapConfig);
        vec2 sds = vec3to2(mapConfig->convertor->convert(vec2to3(navPos, 0),
            mapConfig->referenceFrame.model.navigationSrs, it.second.srs));
        if (!ni.inside(vecToUblas<math::Point2>(sds)))
            continue;
        return std::make_pair(ni, sds);
    }
    LOGTHROW(fatal, std::invalid_argument) << "Impossible position";
    throw; // shut up compiler warning
}

const NodeInfo MapImpl::findInfoSdsSampled(const NodeInfo &info,
                                           const vec2 &sdsPos)
{
    double desire = std::log2(options.navigationSamplesPerViewExtent
            * info.extents().size() / mapConfig->position.verticalExtent);
    if (desire < 3)
        return info;
    
    vtslibs::vts::Children childs = vtslibs::vts::children(info.nodeId());
    for (uint32 i = 0; i < childs.size(); i++)
    {
        NodeInfo ni = info.child(childs[i]);
        if (!ni.inside(vecToUblas<math::Point2>(sdsPos)))
            continue;
        return findInfoSdsSampled(ni, sdsPos);
    }
    LOGTHROW(fatal, std::invalid_argument) << "Impossible position";
    throw; // shut up compiler warning
}

void MapImpl::resetPositionAltitude(double resetOffset)
{
    navigation.inertiaMotion(2) = 0;
    navigation.lastPanZShift.reset();
    std::queue<std::shared_ptr<class HeightRequest>>().swap(
                navigation.panZQueue);
    auto r = std::make_shared<HeightRequest>(
                vec3to2(vecFromUblas<vec3>(mapConfig->position.position)));
    r->resetOffset = resetOffset;
    navigation.panZQueue.push(r);
}

void MapImpl::resetPositionRotation(bool immediate)
{
    vtslibs::registry::Position &pos = mapConfig->position;
    if (immediate)
    {
        navigation.inertiaRotation = vec3(0,0,0);
        pos.orientation = math::Point3(0,270,0);
    }
    else
    {
        vec3 c = -vecFromUblas<vec3>(pos.orientation);
        for (int i = 0; i < 3; i++)
            normalizeAngle(c(i));
        if (c(0) > 180)
            c(0) = c(0) - 360;
        c(1) = c(1) - 90;
        if (c(2) > 180)
            c(2) = c(2) - 360;
        navigation.inertiaRotation = c;
    }
    navigation.autoMotion = navigation.autoRotation = vec3(0,0,0);
    resetNavigationMode();
}

void MapImpl::resetNavigationMode()
{
    if (navigation.navigationMode == MapOptions::NavigationMode::Dynamic)
        navigation.navigationMode = MapOptions::NavigationMode::Azimuthal;
    else
        navigation.navigationMode = options.navigationMode;
}

void MapImpl::convertPositionSubjObj()
{
    vtslibs::registry::Position &pos = mapConfig->position;
    vec3 center, dir, up;
    positionToCamera(center, dir, up);
    double dist = positionObjectiveDistance();
    if (pos.type == vtslibs::registry::Position::Type::objective)
        dist *= -1;
    center += dir * dist;
    pos.position = vecToUblas<math::Point3>(
                mapConfig->convertor->physToNav(center));
}

void MapImpl::positionToCamera(vec3 &center, vec3 &dir, vec3 &up)
{
    vtslibs::registry::Position &pos = mapConfig->position;
    
    // camera-space vectors
    vec3 rot = vecFromUblas<vec3>(pos.orientation);
    center = vecFromUblas<vec3>(pos.position);
    dir = vec3(1, 0, 0);
    up = vec3(0, 0, -1);
    
    // apply rotation
    double yaw = mapConfig->srs.get
            (mapConfig->referenceFrame.model.navigationSrs).type
            == vtslibs::registry::Srs::Type::projected
            ? rot(0) : -rot(0);
    mat3 tmp = upperLeftSubMatrix(rotationMatrix(2, yaw))
            * upperLeftSubMatrix(rotationMatrix(1, -rot(1)));
    dir = tmp * dir;
    up = tmp * up;
    
    // transform to physical srs
    switch (mapConfig->srs.get
            (mapConfig->referenceFrame.model.navigationSrs).type)
    {
    case vtslibs::registry::Srs::Type::projected:
    {
        // swap XY
        std::swap(dir(0), dir(1));
        std::swap(up(0), up(1));
        // invert Z
        dir(2) *= -1;
        up(2) *= -1;
        // add center of orbit (transform to navigation srs)
        dir += center;
        up += center;
        // transform to physical srs
        center = mapConfig->convertor->navToPhys(center);
        dir = mapConfig->convertor->navToPhys(dir);
        up = mapConfig->convertor->navToPhys(up);
        // points -> vectors
        dir = normalize(dir - center);
        up = normalize(up - center);
    } break;
    case vtslibs::registry::Srs::Type::geographic:
    {
        // find lat-lon coordinates of points moved to north and east
        vec3 n2 = mapConfig->convertor->navGeodesicDirect(center, 0, 100);
        vec3 e2 = mapConfig->convertor->navGeodesicDirect(center, 90, 100);
        // transform to physical srs
        center = mapConfig->convertor->navToPhys(center);
        vec3 n = mapConfig->convertor->navToPhys(n2);
        vec3 e = mapConfig->convertor->navToPhys(e2);
        // points -> vectors
        n = normalize(n - center);
        e = normalize(e - center);
        // construct NED coordinate system
        vec3 d = normalize(cross(n, e));
        e = normalize(cross(n, d));
        mat3 tmp = (mat3() << n, e, d).finished();
        // rotate original vectors
        dir = tmp * dir;
        up = tmp * up;
        dir = normalize(dir);
        up = normalize(up);
    } break;
    default:
        LOGTHROW(fatal, std::invalid_argument) << "Invalid srs type";
    }
}

double MapImpl::positionObjectiveDistance()
{
    vtslibs::registry::Position &pos = mapConfig->position;
    return pos.verticalExtent
            * 0.5 / tan(degToRad(pos.verticalFov * 0.5));
}

void MapImpl::updateNavigation()
{
    assert(options.cameraInertiaPan >= 0
           && options.cameraInertiaPan < 1);
    assert(options.cameraInertiaRotate >= 0
           && options.cameraInertiaRotate < 1);
    assert(options.cameraInertiaZoom >= 0
           && options.cameraInertiaZoom < 1);
    assert(options.navigationLatitudeThreshold > 0
           && options.navigationLatitudeThreshold < 90);

    vtslibs::registry::Position &pos = mapConfig->position;
    vec3 position = vecFromUblas<vec3>(pos.position);
    vec3 rotation = vecFromUblas<vec3>(pos.orientation);

    // floating position
    if (pos.heightMode == vtslibs::registry::Position::HeightMode::floating)
    {
        pos.heightMode = vtslibs::registry::Position::HeightMode::fixed;
        resetPositionAltitude(pos.position[2]);
    }
    assert(pos.heightMode == vtslibs::registry::Position::HeightMode::fixed);
    
    checkPanZQueue(position(2));

    // limit zoom
    pos.verticalExtent = clamp(pos.verticalExtent,
                               options.positionViewExtentMin,
                               options.positionViewExtentMax);

    // apply auto motion & rotate
    navigation.inertiaMotion += navigation.autoMotion;
    navigation.inertiaRotation += navigation.autoRotation;
    
    // position inertia
    {
        vec3 move = navigation.inertiaMotion
                * (1.0 - options.cameraInertiaPan);
        navigation.inertiaMotion *= options.cameraInertiaPan;
        mat3 rot = upperLeftSubMatrix(rotationMatrix(2, -rotation(0)));
        move = rot * move * (pos.verticalExtent / 800);
        switch (mapConfig->srs.get
                (mapConfig->referenceFrame.model.navigationSrs).type)
        {
        case vtslibs::registry::Srs::Type::projected:
        {
            position += move;
        } break;
        case vtslibs::registry::Srs::Type::geographic:
        {
            vec3 &p = position;
            // todo change to single geodesic
            p = mapConfig->convertor->navGeodesicDirect(p, 90, move(0));
            p = mapConfig->convertor->navGeodesicDirect(p, 0, move(1));
            p(0) = modulo(p(0) + 180, 360) - 180;
    
            // try to switch to free mode
            if (options.navigationMode == MapOptions::NavigationMode::Dynamic)
            {
                if (abs(p(1)) >= options.navigationLatitudeThreshold - 1e-5)
                    navigation.navigationMode
                            = MapOptions::NavigationMode::Free;
            }
            else
                navigation.navigationMode = options.navigationMode;
    
            // limit latitude
            switch (navigation.navigationMode)
            {
            case MapOptions::NavigationMode::Free:
            { // wraps on poles
                // todo
            } break;
            case MapOptions::NavigationMode::Azimuthal:
            { // clamp
                p(1) = clamp(p(1),
                        -options.navigationLatitudeThreshold,
                        options.navigationLatitudeThreshold);
            } break;
            default:
                LOGTHROW(fatal, std::invalid_argument)
                        << "Invalid navigation mode";
            }
        } break;
        default:
            LOGTHROW(fatal, std::invalid_argument) << "Invalid srs type";
        }
    }

    // rotation inertia
    {
        rotation += navigation.inertiaRotation
                * (1.0 - options.cameraInertiaRotate);
        navigation.inertiaRotation *= options.cameraInertiaRotate;
    }

    // zoom inertia
    {
        double c = navigation.inertiaViewExtent
                * (1.0 - options.cameraInertiaZoom);
        navigation.inertiaViewExtent *= options.cameraInertiaZoom;
        pos.verticalExtent *= pow(1.001, -c);
    }

    // normalize angles
    normalizeAngle(rotation[0]);
    rotation[1] = clamp(rotation[1], 270, 350);
    normalizeAngle(rotation[2]);
    
    assert(position[0] >= -180 && position[0] <= 180);
    assert(position[1] >= -90 && position[1] <= 90);
    
    // vertical camera adjustment
    {
        auto h = std::make_shared<HeightRequest>(vec3to2(position));
        if (navigation.panZQueue.size() < 2)
            navigation.panZQueue.push(h);
        else
            navigation.panZQueue.back() = h;
    }
    
    // store changed values
    pos.position = vecToUblas<math::Point3>(position);
    pos.orientation = vecToUblas<math::Point3>(rotation);
}

void MapImpl::pan(const vec3 &value)
{
    const vec3 scale(-1, 1, 0);
    for (int i = 0; i < 3; i++)
        navigation.inertiaMotion(i) += value[i] * scale(i)
                * options.cameraSensitivityPan;
    navigation.autoMotion = navigation.autoRotation = vec3(0, 0, 0);
}

void MapImpl::rotate(const vec3 &value)
{
    const vec3 scale(0.2, -0.1, 0);
    for (int i = 0; i < 3; i++)
        navigation.inertiaRotation(i) += value[i] * scale(i)
                * options.cameraSensitivityRotate;
    navigation.autoMotion = navigation.autoRotation = vec3(0, 0, 0);
}

void MapImpl::zoom(double value)
{
    navigation.inertiaViewExtent += value
            * options.cameraSensitivityZoom;
    navigation.autoMotion = navigation.autoRotation = vec3(0, 0, 0);
}

} // namespace vts