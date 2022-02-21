#include "renderer.h"

#include <Tempest/Color>
#include <Tempest/Fence>
#include <Tempest/Log>

#include "graphics/mesh/submesh/staticmesh.h"
#include "ui/inventorymenu.h"
#include "camera.h"
#include "gothic.h"

using namespace Tempest;

Renderer::Renderer(Tempest::Swapchain& swapchain)
  : swapchain(swapchain) {
  auto& device = Resources::device();
  view.identity();

  static const TextureFormat shfrm[] = {
    TextureFormat::R16,
    TextureFormat::RG16,
    TextureFormat::R32F,
    TextureFormat::RGBA8,
    };
  static const TextureFormat zfrm[] = {
    //TextureFormat::Depth24S8,
    TextureFormat::Depth24x8,
    TextureFormat::Depth16,
    };

  for(auto& i:shfrm) {
    if(device.properties().hasAttachFormat(i) && device.properties().hasSamplerFormat(i)){
      shadowFormat = i;
      break;
      }
    }

  for(auto& i:zfrm) {
    if(device.properties().hasDepthFormat(i)){
      zBufferFormat = i;
      break;
      }
    }

  Log::i("GPU = ",device.properties().name);
  Log::i("Depth format = ",int(zBufferFormat)," Shadow format = ",int(shadowFormat));

  Gothic::inst().onSettingsChanged.bind(this,&Renderer::initSettings);
  initSettings();
  }

Renderer::~Renderer() {
  Gothic::inst().onSettingsChanged.ubind(this,&Renderer::initSettings);
  }

void Renderer::resetSwapchain() {
  auto& device = Resources::device();

  const uint32_t w      = swapchain.w();
  const uint32_t h      = swapchain.h();
  const uint32_t smSize = 2048;

  zbuffer        = device.zbuffer(zBufferFormat,w,h);
  zbufferItem    = device.zbuffer(zBufferFormat,w,h);

  for(int i=0; i<2; ++i) {
    shadowMap[i] = device.attachment (shadowFormat, smSize,smSize);
    shadowZ[i]   = device.zbuffer    (zBufferFormat,smSize,smSize);
    }

  lightingBuf = device.attachment(TextureFormat::RGBA8,swapchain.w(),swapchain.h());
  gbufDiffuse = device.attachment(TextureFormat::RGBA8,swapchain.w(),swapchain.h());
  gbufNormal  = device.attachment(TextureFormat::RGBA8,swapchain.w(),swapchain.h());
  gbufDepth   = device.attachment(TextureFormat::R32F, swapchain.w(),swapchain.h());

  if(auto wview=Gothic::inst().worldView()) {
    wview->setFrameGlobals(nullptr,0,0);
    wview->setGbuffer(Resources::fallbackBlack(),Resources::fallbackBlack(),Resources::fallbackBlack(),Resources::fallbackBlack());
    }

  auto smp = Sampler2d::nearest();
  smp.setClamping(ClampMode::ClampToEdge);

  auto smpB = Sampler2d::bilinear();
  smpB.setClamping(ClampMode::ClampToEdge);

  uboCopy = device.descriptors(Shaders::inst().copy);
  uboCopy.set(0,lightingBuf,Sampler2d::nearest());

  ssao.ssaoBuf = device.attachment(ssao.aoFormat, (swapchain.w()+1)/2,(swapchain.h()+1)/2);
  ssao.blurBuf = device.attachment(ssao.aoFormat, (swapchain.w()+1)/2,(swapchain.h()+1)/2);

  ssao.uboSsao = device.descriptors(Shaders::inst().ssao);
  ssao.uboSsao.set(0,lightingBuf,smp);
  ssao.uboSsao.set(1,gbufDiffuse,smp);
  ssao.uboSsao.set(2,gbufNormal, smp);
  ssao.uboSsao.set(3,gbufDepth,  smpB);

  ssao.uboCompose = device.descriptors(Shaders::inst().ssaoCompose);
  ssao.uboCompose.set(0,lightingBuf, smpB);
  ssao.uboCompose.set(1,gbufDiffuse, smpB);
  ssao.uboCompose.set(2,ssao.ssaoBuf,smpB);

  ssao.uboBlur[0] = device.descriptors(Shaders::inst().bilateralBlur);
  ssao.uboBlur[0].set(0,ssao.ssaoBuf,smp);
  ssao.uboBlur[0].set(1,gbufDepth,   smpB);

  ssao.uboBlur[1] = device.descriptors(Shaders::inst().bilateralBlur);
  ssao.uboBlur[1].set(0,ssao.blurBuf,smp);
  ssao.uboBlur[1].set(1,gbufDepth,   smpB);
  }

void Renderer::initSettings() {
  settings.zEnvMappingEnabled = Gothic::inst().settingsGetI("ENGINE","zEnvMappingEnabled")!=0;
  settings.zCloudShadowScale  = Gothic::inst().settingsGetI("ENGINE","zCloudShadowScale") !=0;
  }

void Renderer::onWorldChanged() {
  }

void Renderer::setCameraView(const Camera& camera) {
  view     = camera.view();
  proj     = camera.projective();
  viewProj = camera.viewProj();
  if(auto wview=Gothic::inst().worldView()) {
    for(size_t i=0; i<Resources::ShadowLayers; ++i)
      shadow[i] = camera.viewShadow(wview->mainLight().dir(),i);
    }

  const float zNear = camera.zNear();
  const float zFar  = camera.zFar();
  clipInfo.x = zNear*zFar;
  clipInfo.y = zNear-zFar;
  clipInfo.z = zFar;
  }

void Renderer::draw(Encoder<CommandBuffer>& cmd, uint8_t cmdId, size_t imgId,
                    VectorImage::Mesh& uiLayer, VectorImage::Mesh& numOverlay,
                    InventoryMenu& inventory) {
  auto& result = swapchain[imgId];

  draw(result, cmd, cmdId);
  cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}});
  uiLayer.draw(cmd);

  if(inventory.isOpen()!=InventoryMenu::State::Closed) {
    cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}},{zbufferItem, 1.f, Tempest::Preserve});
    inventory.draw(cmd,cmdId);

    cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}});
    numOverlay.draw(cmd);
    }
  }

void Renderer::draw(Tempest::Attachment& result, Tempest::Encoder<CommandBuffer>& cmd, uint8_t cmdId) {
  auto wview = Gothic::inst().worldView();
  if(wview==nullptr) {
    cmd.setFramebuffer({{result, Vec4(), Tempest::Preserve}});
    return;
    }

  wview->setViewProject(view,proj);
  wview->setModelView(viewProj,shadow,Resources::ShadowLayers);
  const Texture2d* sh[Resources::ShadowLayers];
  for(size_t i=0; i<Resources::ShadowLayers; ++i)
    sh[i] = &textureCast(shadowMap[i]);
  wview->setFrameGlobals(sh,Gothic::inst().world()->tickCount(),cmdId);
  wview->setGbuffer(textureCast(lightingBuf),textureCast(gbufDiffuse),textureCast(gbufNormal),textureCast(gbufDepth));

  {
  Frustrum f[SceneGlobals::V_Count];
  f[SceneGlobals::V_Shadow0].make(shadow[0],shadowMap[0].w(),shadowMap[0].h());
  f[SceneGlobals::V_Shadow1].make(shadow[1],shadowMap[1].w(),shadowMap[1].h());
  f[SceneGlobals::V_Main   ].make(viewProj,zbuffer.w(),zbuffer.h());
  wview->visibilityPass(f);
  }

  for(uint8_t i=0; i<Resources::ShadowLayers; ++i) {
    cmd.setFramebuffer({{shadowMap[i], Vec4(), Tempest::Preserve}}, {shadowZ[i], 0.f, Tempest::Preserve});
    wview->drawShadow(cmd,cmdId,i);
    }

  wview->prepareSky(cmd,cmdId);

  cmd.setFramebuffer({{lightingBuf, Vec4(),           Tempest::Preserve},
                      {gbufDiffuse, Tempest::Discard, Tempest::Preserve},
                      {gbufNormal,  Tempest::Discard, Tempest::Preserve},
                      {gbufDepth,   1.f,              Tempest::Preserve}},
                     {zbuffer, 1.f, Tempest::Preserve});
  wview->drawGBuffer(cmd,cmdId);

  drawSSAO(result,cmd,*wview);

  cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}}, {zbuffer, Tempest::Preserve, Tempest::Discard});
  wview->drawLights (cmd,cmdId);
  wview->drawSky    (cmd,cmdId);
  wview->drawMain   (cmd,cmdId);
  wview->drawFog    (cmd,cmdId);
  }

void Renderer::drawSSAO(Tempest::Attachment& result, Encoder<CommandBuffer>& cmd, const WorldView& view) {
  static bool useSsao = true;

  if(!useSsao || !settings.zCloudShadowScale) {
    cmd.setFramebuffer({{result, Tempest::Discard, Tempest::Preserve}});
    cmd.setUniforms(Shaders::inst().copy,uboCopy);
    cmd.draw(Resources::fsqVbo());
    return;
    }

  {
  // ssao
  struct PushSsao {
    Matrix4x4 mvp;
  } push;
  push.mvp = viewProj;

  cmd.setFramebuffer({{ssao.ssaoBuf, Tempest::Discard, Tempest::Preserve}});
  cmd.setUniforms(Shaders::inst().ssao,ssao.uboSsao,&push,sizeof(push));
  cmd.draw(Resources::fsqVbo());
  }

  for(int i=0; i<2; ++i) {
    struct PushSsao {
      Vec3  clipInfo;
      float sharpness;
      Vec2  invResolutionDirection;
    } push;
    push.clipInfo               = clipInfo;
    push.sharpness              = 1.f;
    push.invResolutionDirection = Vec2(1.f/float(ssao.ssaoBuf.w()), 1.f/float(ssao.ssaoBuf.h()));
    if(i==0)
      push.invResolutionDirection.y = 0; else
      push.invResolutionDirection.x = 0;

    if(i==0)
      cmd.setFramebuffer({{ssao.blurBuf, Tempest::Discard, Tempest::Preserve}}); else
      cmd.setFramebuffer({{ssao.ssaoBuf, Tempest::Discard, Tempest::Preserve}});
    cmd.setUniforms(Shaders::inst().bilateralBlur,ssao.uboBlur[i],&push,sizeof(push));
    cmd.draw(Resources::fsqVbo());
    }

  {
  struct PushSsao {
    Vec3 ambient;
  } push;
  push.ambient = view.ambientLight();
  cmd.setFramebuffer({{result, Tempest::Discard, Tempest::Preserve}});
  cmd.setUniforms(Shaders::inst().ssaoCompose,ssao.uboCompose,&push,sizeof(push));
  cmd.draw(Resources::fsqVbo());
  }
  }

Tempest::Attachment Renderer::screenshoot(uint8_t frameId) {
  auto& device = Resources::device();
  device.waitIdle();

  uint32_t w    = uint32_t(zbuffer.w());
  uint32_t h    = uint32_t(zbuffer.h());
  auto     img  = device.attachment(Tempest::TextureFormat::RGBA8,w,h);

  if(auto wview = Gothic::inst().worldView())
    wview->setupUbo();

  CommandBuffer cmd;
  {
  auto enc = cmd.startEncoding(device);
  draw(img,enc,frameId);
  }

  Fence sync = device.fence();
  device.submit(cmd,sync);
  sync.wait();

  if(auto wview = Gothic::inst().worldView())
    wview->setupUbo();

  return img;
  }
