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
  uboCopy = device.descriptors(Shaders::inst().copy.layout());
  uboHiZ  = device.descriptors(Shaders::inst().copy.layout());
  }

void Renderer::resetSwapchain() {
  auto& device = Resources::device();

  const uint32_t w      = swapchain.w();
  const uint32_t h      = swapchain.h();
  const uint32_t imgC   = swapchain.imageCount();
  const uint32_t smSize = 2048;

  zbuffer        = device.zbuffer(zBufferFormat,w,h);
  zbufferItem    = device.zbuffer(zBufferFormat,w,h);
  shadowPass     = device.pass(FboMode(FboMode::PreserveOut,Color(0.0)), FboMode(FboMode::Discard,0.f));

  for(int i=0; i<Resources::ShadowLayers; ++i){
    shadowMap[i] = device.attachment (shadowFormat, smSize,smSize);
    shadowZ[i]   = device.zbuffer    (zBufferFormat,smSize,smSize);
    fboShadow[i] = device.frameBuffer(shadowMap[i],shadowZ[i]);
    }

  lightingBuf = device.attachment(TextureFormat::RGBA8,swapchain.w(),swapchain.h());
  gbufDiffuse = device.attachment(TextureFormat::RGBA8,swapchain.w(),swapchain.h());
  gbufNormal  = device.attachment(TextureFormat::RGBA8,swapchain.w(),swapchain.h());
  gbufDepth   = device.attachment(TextureFormat::R32F, swapchain.w(),swapchain.h());

  hiZCpu      = Pixmap(hiZ.w(),hiZ.h(),Pixmap::Format::R32F);
  hiZ         = device.attachment(TextureFormat::R32F, swapchain.w()/16,swapchain.h()/16);
  fboHiZ      = device.frameBuffer(hiZ);
  for(int i=0; i<Resources::MaxFramesInFlight; ++i) {
    size_t sz = hiZ.w()*hiZ.h()*sizeof(float);
    bufHiZ[i] = device.ssbo(BufferHeap::Readback,nullptr,sz);
    }

  fboUi.clear();
  fbo3d.clear();
  fboCpy.clear();
  fboItem.clear();
  for(uint32_t i=0;i<imgC;++i) {
    Tempest::Attachment& frame=swapchain.image(i);
    fbo3d  .emplace_back(device.frameBuffer(frame,zbuffer));
    fboCpy .emplace_back(device.frameBuffer(frame));
    fboItem.emplace_back(device.frameBuffer(frame,zbufferItem));
    fboUi  .emplace_back(device.frameBuffer(frame));
    }
  fboGBuf     = device.frameBuffer(lightingBuf,gbufDiffuse,gbufNormal,gbufDepth,zbuffer);
  copyPass    = device.pass(FboMode::PreserveOut);

  if(auto wview=Gothic::inst().worldView()) {
    wview->setFrameGlobals(nullptr,0,0);
    wview->setGbuffer(Resources::fallbackBlack(),Resources::fallbackBlack(),Resources::fallbackBlack(),Resources::fallbackBlack());
    }

  uboCopy.set(0,lightingBuf,Sampler2d::nearest());
  uboHiZ .set(0,gbufDepth,  Sampler2d::nearest());

  gbufPass       = device.pass(FboMode(FboMode::PreserveOut,Color(0.0)),
                               FboMode(FboMode::PreserveOut),
                               FboMode(FboMode::PreserveOut),
                               FboMode(FboMode::PreserveOut,Color(1.0)),
                               FboMode(FboMode::PreserveOut,1.f));
  gbufPass2      = device.pass(FboMode(FboMode::Preserve),
                               FboMode(FboMode::Preserve),
                               FboMode(FboMode::Preserve),
                               FboMode(FboMode::Preserve),
                               FboMode(FboMode::Preserve));
  mainPass       = device.pass(FboMode::Preserve, FboMode::PreserveIn);
  mainPassNoGbuf = device.pass(FboMode(FboMode::PreserveOut,Color(0.0)), FboMode(FboMode::Discard,1.f));
  uiPass         = device.pass(FboMode::Preserve);
  inventoryPass  = device.pass(FboMode::Preserve, FboMode(FboMode::Discard,1.f));
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
  }

void Renderer::draw(Encoder<CommandBuffer>& cmd, uint8_t cmdId, size_t imgId,
                    VectorImage::Mesh& uiLayer, VectorImage::Mesh& numOverlay,
                    InventoryMenu& inventory) {
  draw(cmd, fbo3d  [imgId], fboCpy[imgId], fboHiZ, cmdId);
  draw(cmd, fboUi  [imgId], uiLayer);
  draw(cmd, fboItem[imgId], inventory, cmdId);
  draw(cmd, fboUi  [imgId], numOverlay);
  }

void Renderer::draw(Tempest::Encoder<CommandBuffer>& cmd,
                    FrameBuffer& fbo, FrameBuffer& fboCpy, FrameBuffer& fboHiZ, uint8_t cmdId) {
  auto wview = Gothic::inst().worldView();
  if(wview==nullptr) {
    cmd.setFramebuffer(fbo,mainPassNoGbuf);
    return;
    }

  auto& device = Resources::device();

  static bool useHiZ = true;
  if(useHiZ) {
    device.readBytes(bufHiZ[cmdId],hiZCpu.data(),bufHiZ[cmdId].size());
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
  f[SceneGlobals::V_Shadow0].make(shadow[0],fboShadow[0].w(),fboShadow[0].h());
  f[SceneGlobals::V_Shadow1].make(shadow[1],fboShadow[1].w(),fboShadow[1].h());
  f[SceneGlobals::V_Main   ].make(viewProj,fbo.w(),fbo.h());
  wview->visibilityPass(f,hiZCpu);
  }

  for(uint8_t i=0; i<Resources::ShadowLayers; ++i) {
    cmd.setFramebuffer(fboShadow[i],shadowPass);
    wview->drawShadow(cmd,cmdId,i);
    }

  cmd.setFramebuffer(fboGBuf,gbufPass);
  wview->drawGBufferOcc(cmd,cmdId);

  {
  cmd.setFramebuffer(fboHiZ,copyPass);
  cmd.setUniforms(Shaders::inst().copy,uboHiZ);
  cmd.draw(Resources::fsqVbo());
  cmd.setFramebuffer(nullptr);
  cmd.copy(hiZ,0,bufHiZ[cmdId],0);
  }

  cmd.setFramebuffer(fboGBuf,gbufPass2);
  wview->drawGBuffer(cmd,cmdId);

  cmd.setFramebuffer(fboCpy,copyPass);
  cmd.setUniforms(Shaders::inst().copy,uboCopy);
  cmd.draw(Resources::fsqVbo());

  cmd.setFramebuffer(fbo,mainPass);
  wview->drawLights (cmd,cmdId);
  wview->drawMain   (cmd,cmdId);
  }

void Renderer::draw(Tempest::Encoder<CommandBuffer>& cmd, FrameBuffer& fbo, InventoryMenu &inventory, uint8_t cmdId) {
  if(inventory.isOpen()==InventoryMenu::State::Closed)
    return;
  cmd.setFramebuffer(fbo,inventoryPass);
  inventory.draw(fbo,cmd,cmdId);
  }

void Renderer::draw(Tempest::Encoder<CommandBuffer>& cmd, FrameBuffer& fbo, VectorImage::Mesh& surface) {
  cmd.setFramebuffer(fbo,uiPass);
  surface.draw(cmd);
  }

Tempest::Attachment Renderer::screenshoot(uint8_t frameId) {
  auto& device = Resources::device();
  device.waitIdle();

  uint32_t w = uint32_t(zbuffer.w());
  uint32_t h = uint32_t(zbuffer.h());

  auto        img  = device.attachment(Tempest::TextureFormat::RGBA8,w,h);
  FrameBuffer fbo  = device.frameBuffer(img,zbuffer);
  FrameBuffer fboC = device.frameBuffer(img);

  if(auto wview = Gothic::inst().worldView())
    wview->setupUbo();

  CommandBuffer cmd;
  {
  auto enc = cmd.startEncoding(device);
  draw(enc,fbo,fboC,fboHiZ,frameId);
  }

  Fence sync = device.fence();
  device.submit(cmd,sync);
  sync.wait();

  if(auto wview = Gothic::inst().worldView())
    wview->setupUbo();

  return img;
  }
