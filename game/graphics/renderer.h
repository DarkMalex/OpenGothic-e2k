#pragma once

#include <Tempest/RenderPipeline>
#include <Tempest/CommandBuffer>
#include <Tempest/Matrix4x4>
#include <Tempest/Widget>
#include <Tempest/Device>
#include <Tempest/UniformBuffer>
#include <Tempest/VectorImage>

#include "worldview.h"
#include "shaders.h"

class Camera;
class InventoryMenu;

class Renderer final {
  public:
    Renderer(Tempest::Swapchain& swapchain);
    ~Renderer();

    void resetSwapchain();
    void onWorldChanged();

    void setCameraView(const Camera &camera);

    void draw(Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint8_t cmdId, size_t imgId,
              Tempest::VectorImage::Mesh& uiLayer, Tempest::VectorImage::Mesh& numOverlay,
              InventoryMenu &inventory);

    Tempest::Attachment       screenshoot(uint8_t frameId);

  private:
    void drawSSAO(Tempest::Attachment& result, Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& view);
    void draw    (Tempest::Attachment& result, Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint8_t cmdId);
    void initSettings();

    struct Settings {
      bool zEnvMappingEnabled = false;
      bool zCloudShadowScale  = false;
      } settings;

    Tempest::Swapchain&       swapchain;
    Tempest::Matrix4x4        view, proj, viewProj;
    Tempest::Matrix4x4        shadow[Resources::ShadowLayers];
    Tempest::Vec3             clipInfo;

    Tempest::Attachment       shadowMap[Resources::ShadowLayers];
    Tempest::ZBuffer          zbuffer, zbufferItem, shadowZ[Resources::ShadowLayers];

    Tempest::Attachment       lightingBuf;
    Tempest::Attachment       gbufDiffuse;
    Tempest::Attachment       gbufNormal;
    Tempest::Attachment       gbufDepth;

    struct SSAO {
      Tempest::TextureFormat  aoFormat = Tempest::TextureFormat::R8;
      Tempest::Attachment     ssaoBuf;
      Tempest::Attachment     blurBuf;
      Tempest::DescriptorSet  uboSsao, uboBlur[2], uboCompose;
    } ssao;

    Tempest::TextureFormat    shadowFormat  = Tempest::TextureFormat::RGBA8;
    Tempest::TextureFormat    zBufferFormat = Tempest::TextureFormat::Depth16;

    Tempest::DescriptorSet    uboCopy;
    Shaders                   stor;
  };
