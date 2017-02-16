#ifndef MAPRESOURCES_H_hdsgfhjuwebgfj
#define MAPRESOURCES_H_hdsgfhjuwebgfj

#include <memory>

#include "../../vts-libs/vts/metatile.hpp"
#include "../../vts-libs/vts/mapconfig.hpp"

#include "foundation.h"
#include "resource.h"

namespace melown
{
    class MapConfig : public Resource, public vadstena::vts::MapConfig
    {
    public:
        void load(const std::string &name, class Map *base) override;

        std::string basePath;
    };

    class MetaTile : public Resource, public vadstena::vts::MetaTile
    {
    public:
        MetaTile();
        void load(const std::string &name, class Map *base) override;
    };

    class MeshAggregate : public Resource
    {
    public:
        std::vector<std::shared_ptr<class GpuMeshRenderable>> submeshes;

        void load(const std::string &name, class Map *base) override;
    };
}

#endif
