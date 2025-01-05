#pragma once

#include "libavutil/frame.h"
#include <memory>
#include <pathfinder/common/color.h>
#include <pathfinder/common/math/mat4.h>
#include <pathfinder/common/math/vec3.h>
#include <pathfinder/gpu/device.h>
#include <pathfinder/gpu/queue.h>
#include <pathfinder/gpu/render_pipeline.h>
#include <pathfinder/gpu/texture.h>
#include <vector>

// class YUVData {
// public:
//     QByteArray Y;
//     QByteArray U;
//     QByteArray V;
//     int yLineSize;
//     int uLineSize;
//     int vLineSize;
//     int height;
// };

class YuvRenderer {
public:
    YuvRenderer(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue);
    ~YuvRenderer() = default;
    void init();
    void render(std::shared_ptr<Pathfinder::Texture> outputTex);
    void resize(int width, int height);
    void updateTextureInfo(int width, int height, int format);
    void updateTextureData(const std::shared_ptr<AVFrame> &data);
    void clear();

protected:
    void initPipeline();
    void initGeometry();

private:
    std::shared_ptr<Pathfinder::RenderPipeline> mPipeline;
    std::shared_ptr<Pathfinder::Queue> mQueue;
    std::shared_ptr<Pathfinder::RenderPass> mRenderPass;
    std::shared_ptr<Pathfinder::Texture> mTexY;
    std::shared_ptr<Pathfinder::Texture> mTexU;
    std::shared_ptr<Pathfinder::Texture> mTexV;
    std::shared_ptr<Pathfinder::DescriptorSet> mDescriptorSet;
    std::shared_ptr<Pathfinder::Sampler> mSampler;
    std::shared_ptr<Pathfinder::Buffer> mVertexBuffer;
    std::shared_ptr<Pathfinder::Buffer> mUniformBuffer;

    std::shared_ptr<Pathfinder::Texture> mDummyTex;
    std::vector<Pathfinder::Vec3F> mVertices;
    std::vector<Pathfinder::Vec2F> mTexCoords;

    Pathfinder::Mat4 mModelMatrix;
    Pathfinder::Mat4 mViewMatrix;
    Pathfinder::Mat4 mProjectionMatrix;
    int mPixFmt = 0;
    bool mTextureAllocated = false;

    int m_itemWidth = 0;
    int m_itemHeight = 0;

    bool mNeedClear = false;

    std::shared_ptr<Pathfinder::Device> mDevice;

    volatile bool inited = false;
};
