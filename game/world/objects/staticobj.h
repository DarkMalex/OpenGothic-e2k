#pragma once

#include "physics/physicmesh.h"
#include "graphics/objvisual.h"
#include "vob.h"

class StaticObj : public Vob {
  public:
    StaticObj(Vob* parent, World& world, ZenLoad::zCVobData&& vob, Flags flags);

  private:
    void  moveEvent() override;
    bool  setMobState(std::string_view scheme,int32_t st) override;

    ObjVisual   visual;
    std::string scheme;
  };

