#include "vobbundle.h"

#include <zenload/zenParser.h>

#include "world/world.h"
#include "utils/versioninfo.h"
#include "resources.h"

VobBundle::VobBundle(World& owner, std::string_view filename, Vob::Flags flags) {
  ZenLoad::oCWorldData bundle = Resources::loadVobBundle(filename);
  for(auto& vob:bundle.rootVobs)
    rootVobs.emplace_back(Vob::load(nullptr,owner,std::move(vob),(flags | Vob::Startup)));
  }

void VobBundle::setObjMatrix(const Tempest::Matrix4x4& obj) {
  for(auto& i:rootVobs)
    i->setLocalTransform(obj);
  }
