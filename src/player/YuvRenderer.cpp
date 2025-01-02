#include "YuvRenderer.h"
#include "libavutil/pixfmt.h"

auto vertCode = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

layout(std140) uniform bUniform0 {
    mat4 u_modelMatrix;
    mat4 u_viewMatrix;
    mat4 u_projectMatrix;
};

out vec2 v_texCoord;

void main() {
    gl_Position = u_projectMatrix * u_viewMatrix * u_modelMatrix * vec4(aPos, 1.0f);
    v_texCoord = aUV;
}
)";

auto fragCode =
    R"(
in vec2 v_texCoord;

uniform sampler2D tex_y;
uniform sampler2D tex_u;
uniform sampler2D tex_v;

layout(std140) uniform bUniform1 {
    int pixFmt;
    int pad0;
    int pad1;
    int pad2;
};

void main() {
    vec3 yuv;
    vec3 rgb;
    if (pixFmt == 0 || pixFmt == 12) {
        //yuv420p
        yuv.x = texture2D(tex_y, v_texCoord).r;
        yuv.y = texture2D(tex_u, v_texCoord).r - 0.5;
        yuv.z = texture2D(tex_v, v_texCoord).r - 0.5;
        rgb = mat3( 1.0,       1.0,         1.0,
                    0.0,       -0.3455,  1.779,
                    1.4075, -0.7169,  0.0) * yuv;
    } else if( pixFmt == 23 ){
        // NV12
        yuv.x = texture2D(tex_y, v_texCoord).r;
        yuv.y = texture2D(tex_u, v_texCoord).r - 0.5;
        yuv.z = texture2D(tex_u, v_texCoord).a - 0.5;
        rgb = mat3( 1.0,       1.0,         1.0,
                    0.0,       -0.3455,  1.779,
                    1.4075, -0.7169,  0.0) * yuv;

    } else {
        //YUV444P
        yuv.x = texture2D(tex_y, v_texCoord).r;
        yuv.y = texture2D(tex_u, v_texCoord).r - 0.5;
        yuv.z = texture2D(tex_v, v_texCoord).r - 0.5;

        rgb.x = clamp( yuv.x + 1.402 *yuv.z, 0.0, 1.0);
        rgb.y = clamp( yuv.x - 0.34414 * yuv.y - 0.71414 * yuv.z, 0.0, 1.0);
        rgb.z = clamp( yuv.x + 1.772 * yuv.y, 0.0, 1.0);
    }
    gl_FragColor = vec4(rgb, 1.0);
}
)";

static void safeDeleteTexture(QOpenGLTexture *texture) {
    if (texture) {
        if (texture->isBound()) {
            texture->release();
        }
        if (texture->isCreated()) {
            texture->destroy();
        }
        delete texture;
        texture = nullptr;
    }
}

YuvRenderer::YuvRenderer() {}

void YuvRenderer::init() {

    initPipeline();
    initGeometry();
}
void YuvRenderer::resize(int width, int height) {

    m_itemWidth = width;
    m_itemHeight = height;
    glViewport(0, 0, width, height);
    float bottom = -1.0f;
    float top = 1.0f;
    float n = 1.0f;
    float f = 100.0f;
    mProjectionMatrix.setToIdentity();
    mProjectionMatrix.frustum(-1.0, 1.0, bottom, top, n, f);
}

void YuvRenderer::initPipeline() {
    mVertices << Pathfinder::Vec3F(-1, 1, 0.0f) << Pathfinder::Vec3F(1, 1, 0.0f) << Pathfinder::Vec3F(1, -1, 0.0f)
              << Pathfinder::Vec3F(-1, -1, 0.0f);
    mTexcoords << QVector2D(0, 1) << QVector2D(1, 1) << QVector2D(1, 0) << QVector2D(0, 0);

    mViewMatrix.setToIdentity();
    mViewMatrix.lookAt(
        Pathfinder::Vec3F(0.0f, 0.0f, 1.001f), Pathfinder::Vec3F(0.0f, 0.0f, -5.0f),
        Pathfinder::Vec3F(0.0f, 1.0f, 0.0f));
    mModelMatrix.setToIdentity();

    if (!mProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, VSHCODE)) {
        qWarning() << " add vertex shader file failed.";
        return;
    }
    if (!mProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, FSHCPDE)) {
        qWarning() << " add fragment shader file failed.";
        return;
    }
    mProgram.bindAttributeLocation("qt_Vertex", 0);
    mProgram.bindAttributeLocation("texCoord", 1);
    mProgram.link();
    mProgram.bind();
}

void YuvRenderer::updateTextureInfo(int width, int height, int format) {
    mPixFmt = format;

    mTexY = mDevice->create_texture({ { width, height }, Pathfinder::TextureFormat::R8 }, "y texture");
    // //    mTexY->setFixedSamplePositions(false);
    // mTexY->setMinificationFilter(QOpenGLTexture::Nearest);
    // mTexY->setMagnificationFilter(QOpenGLTexture::Nearest);
    // mTexY->setWrapMode(QOpenGLTexture::ClampToEdge);

    if (format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVJ420P) {
        mTexU = mDevice->create_texture({ { width / 2, height / 2 }, Pathfinder::TextureFormat::R8 }, "u texture");

        mTexV = mDevice->create_texture({ { width / 2, height / 2 }, Pathfinder::TextureFormat::R8 }, "v texture");
    } else if (format == AV_PIX_FMT_NV12) {
        mTexU = mDevice->create_texture({ { width / 2, height / 2 }, Pathfinder::TextureFormat::Rg8 }, "u texture");

        // V is not used for NV12.
        mTexV = mDummyTex;
    }
    //  yuv444p
    else {
        mTexU = mDevice->create_texture({ { width, height }, Pathfinder::TextureFormat::R8 }, "u texture");

        mTexV = mDevice->create_texture({ { width, height }, Pathfinder::TextureFormat::R8 }, "v texture");
    }
    mTextureAllocated = true;
}

void YuvRenderer::updateTextureData(const std::shared_ptr<AVFrame> &data) {
    float frameWidth = m_itemWidth;
    float frameHeight = m_itemHeight;
    if (m_itemWidth * (1.0 * data->height / data->width) < m_itemHeight) {
        frameHeight = frameWidth * (1.0 * data->height / data->width);
    } else {
        frameWidth = frameHeight * (1.0 * data->width / data->height);
    }
    float x = (m_itemWidth - frameWidth) / 2;
    float y = (m_itemHeight - frameHeight) / 2;
    // GL顶点坐标转换
    float x1 = -1 + 2.0 / m_itemWidth * x;
    float y1 = 1 - 2.0 / m_itemHeight * y;
    float x2 = 2.0 / m_itemWidth * frameWidth + x1;
    float y2 = y1 - 2.0 / m_itemHeight * frameHeight;

    mVertices = { Pathfinder::Vec3F(x1, y1, 0.0f), Pathfinder::Vec3F(x2, y1, 0.0f), Pathfinder::Vec3F(x2, y2, 0.0f),
                  Pathfinder::Vec3F(x1, y2, 0.0f) };

    auto encoder = mDevice->create_command_encoder("upload yuv data");

    if (data->linesize[0]) {
        encoder->write_texture(mTexY, {}, data->data[0]);
    }
    if (data->linesize[1]) {
        encoder->write_texture(mTexU, {}, data->data[1]);
    }
    if (data->linesize[2]) {
        encoder->write_texture(mTexV, {}, data->data[2]);
    }

    mQueue->submit_and_wait(encoder);
}

void YuvRenderer::render() {
    glDepthMask(true);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!mTextureAllocated) {
        return;
    }
    if (mNeedClear) {
        mNeedClear = false;
        return;
    }
    mProgram.bind();

    mModelMatHandle = mProgram.uniformLocation("u_modelMatrix");
    mViewMatHandle = mProgram.uniformLocation("u_viewMatrix");
    mProjectMatHandle = mProgram.uniformLocation("u_projectMatrix");
    mVerticesHandle = mProgram.attributeLocation("qt_Vertex");
    mTexCoordHandle = mProgram.attributeLocation("texCoord");
    // 顶点
    mProgram.enableAttributeArray(mVerticesHandle);
    mProgram.setAttributeArray(mVerticesHandle, mVertices.constData());

    // 纹理坐标
    mProgram.enableAttributeArray(mTexCoordHandle);
    mProgram.setAttributeArray(mTexCoordHandle, mTexcoords.constData());

    // MVP矩阵
    mProgram.setUniformValue(mModelMatHandle, mModelMatrix);
    mProgram.setUniformValue(mViewMatHandle, mViewMatrix);
    mProgram.setUniformValue(mProjectMatHandle, mProjectionMatrix);

    // pixFmt
    mProgram.setUniformValue("pixFmt", mPixFmt);

    auto encoder = mDevice->create_command_encoder("render yuv");

    // Update uniform buffers.
    {
        TileUniformD3d9 tile_uniform;
        tile_uniform.tile_size = {TILE_WIDTH, TILE_HEIGHT};
        tile_uniform.texture_metadata_size = {TEXTURE_METADATA_TEXTURE_WIDTH, TEXTURE_METADATA_TEXTURE_HEIGHT};
        tile_uniform.mask_texture_size = {MASK_FRAMEBUFFER_WIDTH,
                                          (float)(MASK_FRAMEBUFFER_HEIGHT * mask_storage.allocated_page_count)};

        // Transform matrix (i.e. the model matrix).
        Mat4 model_mat = Mat4(1.f);
        model_mat = model_mat.translate(Vec3F(-1.f, -1.f, 0.f)); // Move to top-left.
        model_mat = model_mat.scale(Vec3F(2.f / target_texture_size.x, 2.f / target_texture_size.y, 1.f));
        tile_uniform.transform = model_mat;

        tile_uniform.framebuffer_size = target_texture_size.to_f32();
        tile_uniform.z_buffer_size = z_buffer_texture->get_size().to_f32();

        if (color_texture_info) {
            auto color_texture_page = pattern_texture_pages[color_texture_info->page_id];
            if (color_texture_page) {
                color_texture = allocator->get_texture(color_texture_page->texture_id_);
                color_texture_sampler = get_or_create_sampler(color_texture_info->sampling_flags);

                if (color_texture == nullptr) {
                    Logger::error("Failed to obtain color texture!", "RendererD3D9");
                    return;
                }
            }
        }

        tile_uniform.color_texture_size = color_texture->get_size().to_f32();

        // We don't need to preserve the data until the upload commands are implemented because
        // these uniform buffers are host-visible/coherent.
        encoder->write_buffer(allocator->get_buffer(tile_ub_id), 0, sizeof(TileUniformD3d9), &tile_uniform);
    }

    // Update descriptor set.
    mDescriptorSet->add_or_update(
        {
            Pathfinder::Descriptor::sampled(0, Pathfinder::ShaderStage::Fragment, "y", mTexY, mSampler),
            Pathfinder::Descriptor::sampled(1, Pathfinder::ShaderStage::Fragment, "u", mTexU, mSampler),
            Pathfinder::Descriptor::sampled(2, Pathfinder::ShaderStage::Fragment, "v", mTexV, mSampler),
        });

    encoder->begin_render_pass(mRenderPass, mOutputTex, Pathfinder::ColorF::black());

    encoder->draw(0, mVertices.size());

    encoder->end_render_pass();
}

void YuvRenderer::clear() {
    mNeedClear = true;
}
