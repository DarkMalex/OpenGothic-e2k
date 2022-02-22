#pragma once

#include <Tempest/RenderPipeline>
#include <Tempest/Shader>
#include <Tempest/Device>

#include "graphics/objectsbucket.h"

class Material;

class Shaders {
  public:
    Shaders();
    ~Shaders();

    static Shaders& inst();

    Tempest::RenderPipeline lights;
    Tempest::RenderPipeline copy;
    Tempest::RenderPipeline bilateralBlur;
    Tempest::RenderPipeline ssao, ssaoCompose;

    // Nishita
    Tempest::RenderPipeline sky, fog;

    // Scalable and Production Ready Sky and Atmosphere
    Tempest::RenderPipeline skyTransmittance, skyMultiScattering, skyViewLut, skyEGSR;
    Tempest::RenderPipeline fogViewLut, fogEGSR;

    enum PipelineType: uint8_t {
      T_Forward,
      T_Deffered,
      T_Shadow,
      };

    const Tempest::RenderPipeline* materialPipeline(const Material& desc, ObjectsBucket::Type t, PipelineType pt) const;

  private:
    struct ShaderPair {
      Tempest::Shader vs;
      Tempest::Shader fs;
      Tempest::Shader tc, te;
      void load(Tempest::Device &device, const char* tag, const char* format, bool hasTesselation);
      void load(Tempest::Device &device, const char* tag, bool hasTesselation);
      };

    struct MaterialTemplate {
      ShaderPair obj, ani, mph, clr;
      void load(Tempest::Device& device, const char* tag, bool hasTesselation=false);
      };

    struct Entry {
      Tempest::RenderPipeline pipeline;
      Material::AlphaFunc     alpha        = Material::Solid;
      ObjectsBucket::Type     type         = ObjectsBucket::Static;
      PipelineType            pipelineType = PipelineType::T_Forward;
      };

    template<class Vertex>
    Tempest::RenderPipeline pipeline(Tempest::RenderState& st, const ShaderPair &fs) const;

    Tempest::RenderPipeline postEffect(std::string_view name);
    Tempest::RenderPipeline fogShader (std::string_view name);

    static Shaders* instance;

    MaterialTemplate solid,  atest, solidF, atestF, water, ghost, emmision;
    MaterialTemplate shadow, shadowAt;
    mutable std::list<Entry> materials;
  };
