/****************************************************************************
 **
 ** Copyright (C) 2015 The Qt Company Ltd.
 ** Copyright (C) 2015 Ruslan Baratov
 ** Contact: http://www.qt.io/licensing/
 **
 ** This file is part of the examples of the Qt Multimedia module.
 **
 ** $QT_BEGIN_LICENSE:LGPL21$
 ** Commercial License Usage
 ** Licensees holding valid commercial Qt licenses may use this file in
 ** accordance with the commercial license agreement provided with the
 ** Software or, alternatively, in accordance with the terms contained in
 ** a written agreement between you and The Qt Company. For licensing terms
 ** and conditions see http://www.qt.io/terms-conditions. For further
 ** information use the contact form at http://www.qt.io/contact-us.
 **
 ** GNU Lesser General Public License Usage
 ** Alternatively, this file may be used under the terms of the GNU Lesser
 ** General Public License version 2.1 or version 3 as published by the Free
 ** Software Foundation and appearing in the file LICENSE.LGPLv21 and
 ** LICENSE.LGPLv3 included in the packaging of this file. Please review the
 ** following information to ensure the GNU Lesser General Public License
 ** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
 ** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 **
 ** As a special exception, The Qt Company gives you certain additional
 ** rights. These rights are described in The Qt Company LGPL Exception
 ** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
 **
 ** $QT_END_LICENSE$
 **
 ****************************************************************************/

#include "VideoFilterRunnable.hpp"

#include <cassert> // assert

#include <graphics/GLExtra.h> // GATHERER_OPENGL_DEBUG

#include "VideoFilter.hpp"
#include "TextureBuffer.hpp"

#include "libyuv.h"

#include "OGLESGPGPUTest.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>

#include <QDateTime>

struct QVideoFrameScopeMap
{
    QVideoFrameScopeMap(QVideoFrame *frame, QAbstractVideoBuffer::MapMode mode) : frame(frame)
    {
        if(frame)
        {
            status = frame->map(mode);
            if (!status)
            {
                qWarning("Can't map!");
            }
        }
    }
    ~QVideoFrameScopeMap()
    {
        if(frame)
        {
            frame->unmap();
        }
    }
    operator bool() const { return status; }
    QVideoFrame *frame = nullptr;
    bool status = false;
};

static cv::Mat QVideoFrameToCV(QVideoFrame *input);

VideoFilterRunnable::VideoFilterRunnable(VideoFilter *filter) :
m_filter(filter),
m_outTexture(0),
m_lastInputTexture(0)
{
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();

    const char *vendor = (const char *) f->glGetString(GL_VENDOR);
    qDebug("GL_VENDOR: %s", vendor);
}

VideoFilterRunnable::~VideoFilterRunnable() {
    
}

QVideoFrame VideoFilterRunnable::run(QVideoFrame *input, const QVideoSurfaceFormat &surfaceFormat, RunFlags flags)
{
    Q_UNUSED(surfaceFormat);
    Q_UNUSED(flags);
    
    QOpenGLContext * qContext = QOpenGLContext::currentContext();
    QOpenGLFunctions glFuncs(qContext);
    
    float resolution = 1.0f;
    void* glContext = 0;
#if GATHERER_IOS
    glContext = ogles_gpgpu::Core::getCurrentEAGLContext();
    resolution = 2.0f;
#else
    glContext = qContext;
#endif
    if(!m_pipeline)
    {
        QSize size = surfaceFormat.sizeHint();
        GLint backingWidth = size.width(), backingHeight = size.height();

        // TODO: These calls are failing on OS X
        glFuncs.glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &backingWidth);
        glFuncs.glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &backingHeight);
        ogles_gpgpu::Tools::checkGLErr("VideoFilterRunnable", "run");
        
        //glFuncs.gl
        
        cv::Size screenSize(backingWidth, backingHeight);
        m_pipeline = std::make_shared<gatherer::graphics::OEGLGPGPUTest>(glContext, screenSize, resolution);
        m_pipeline->setDoDisplay(false);
    }
    
    // This example supports RGB data only, either in system memory (typical with
    // cameras on all platforms) or as an OpenGL texture (e.g. video playback on
    // OS X).  The latter is the fast path where everything happens on GPU. The
    // former involves a texture upload.
    
    if (!isFrameValid(*input)) {
        qWarning("Invalid input format");
        return *input;
    }
    
    if (isFrameFormatYUV(*input)) {
        qWarning("YUV data is not supported");
        return *input;
    }

    m_outTexture = createTextureForFrame(input);
    auto size = m_pipeline->getOutputSize();
    return TextureBuffer::createVideoFrame(m_outTexture, {size.width, size.height});
}

bool VideoFilterRunnable::isFrameValid(const QVideoFrame& frame) {
    if (!frame.isValid()) {
        return false;
    }
    if (frame.handleType() == QAbstractVideoBuffer::NoHandle) {
        return true;
    }
    if (frame.handleType() == QAbstractVideoBuffer::GLTextureHandle) {
        return true;
    }
    
    return false;
}

bool VideoFilterRunnable::isFrameFormatYUV(const QVideoFrame& frame) {
    if (frame.pixelFormat() == QVideoFrame::Format_YUV420P) {
        return true;
    }
    if (frame.pixelFormat() == QVideoFrame::Format_YV12) {
        return true;
    }
    return false;
}

// Create a texture from the image data.
GLuint VideoFilterRunnable::createTextureForFrame(QVideoFrame* input) {
    QOpenGLContext* openglContext = QOpenGLContext::currentContext();
    if (!openglContext) {
        qWarning("Can't get context!");
        return 0;
    }
    assert(openglContext->isValid());
    
    QOpenGLFunctions *f = openglContext->functions();
    assert(f != 0);
    
    // Already an OpenGL texture.
    if (input->handleType() == QAbstractVideoBuffer::GLTextureHandle) {
        assert(input->pixelFormat() == TextureBuffer::qtTextureFormat());
        GLuint texture = input->handle().toUInt();
        assert(texture != 0);
        f->glBindTexture(GL_TEXTURE_2D, texture);
        m_lastInputTexture = texture;
        
        const cv::Size size(input->width(), input->height());
        void* pixelBuffer = nullptr; //  we are using texture
        const bool useRawPixels = false; //  - // -
        m_pipeline->captureOutput(size, pixelBuffer, useRawPixels, texture);
        
        glActiveTexture(GL_TEXTURE0);
        GLuint outputTexture = m_pipeline->getLastShaderOutputTexture();
        f->glBindTexture(GL_TEXTURE_2D, outputTexture); // TODO: review, unnecessary?
        
        m_outTexture = outputTexture;
    }
    else
    {
        // Scope based pixel buffer lock for non ios platforms
        QVideoFrameScopeMap scopeMap(GATHERER_IOS ? nullptr : input, QAbstractVideoBuffer::ReadOnly);
        if((GATHERER_IOS && !scopeMap) || !GATHERER_IOS) // for non ios platforms
        {
            assert((input->pixelFormat() == QVideoFrame::Format_ARGB32) || (GATHERER_IOS && input->pixelFormat() == QVideoFrame::Format_NV12));

#if defined(Q_OS_IOS) || defined(Q_OS_OSX)
            const GLenum rgbaFormat = GL_BGRA;
#else
            const GLenum rgbaFormat = GL_RGBA;
#endif
    
#if defined(Q_OS_IOS)
            void * const pixelBuffer = input->pixelBufferRef();
#else
            void * const pixelBuffer = input->bits();
#endif
            
            // 0 indicates YUV
            GLenum textureFormat = input->pixelFormat() == QVideoFrame::Format_ARGB32 ? rgbaFormat : 0;
            const bool useRawPixels = !(GATHERER_IOS); // ios uses texture cache / pixel buffer
            const GLuint inputTexture = 0;
            assert(pixelBuffer != nullptr);
            m_pipeline->captureOutput({input->width(), input->height()}, pixelBuffer, useRawPixels, inputTexture, textureFormat);
            
            // QT is expecting GL_TEXTURE0 to be active
            glActiveTexture(GL_TEXTURE0);
            m_outTexture = m_pipeline->getLastShaderOutputTexture();
        }
    }

    const QPoint oldPosition = m_filter->rectanglePosition();
    const QSize rectangleSize(100, 100);
    const bool visible = true;
    const int xDelta = std::rand() % 10;
    const int yDelta = std::rand() % 10;
    const int newX = (oldPosition.x() + xDelta) % input->size().width();
    const int newY = (oldPosition.y() + yDelta) % input->size().height();
    emit m_filter->updateRectangle(QPoint(newX, newY), rectangleSize, visible);

    emit m_filter->updateOutputString(QDateTime::currentDateTime().toString());

    return m_outTexture;
}

// We don't ever want to use this in any practical scenario.
// To be deprecated...
static cv::Mat QVideoFrameToCV(QVideoFrame *input)
{
    cv::Mat frame;
    switch(input->pixelFormat())
    {
        case QVideoFrame::Format_ARGB32:
        case QVideoFrame::Format_BGRA32:
            frame = cv::Mat(input->height(), input->width(), CV_8UC4, input->bits());
            break;
        case QVideoFrame::Format_NV21:
            frame.create(input->height(), input->width(), CV_8UC4);
            libyuv::NV21ToARGB(input->bits(),
                               input->bytesPerLine(),
                               input->bits(1),
                               input->bytesPerLine(1),
                               frame.ptr<uint8_t>(),
                               int(frame.step1()),
                               frame.cols,
                               frame.rows);
            break;
        case QVideoFrame::Format_NV12:
            frame.create(input->height(), input->width(), CV_8UC4);
            libyuv::NV12ToARGB(input->bits(),
                               input->bytesPerLine(),
                               input->bits(1),
                               input->bytesPerLine(1),
                               frame.ptr<uint8_t>(),
                               int(frame.step1()),
                               frame.cols,
                               frame.rows);
            break;
        default: CV_Assert(false);
    }
    return frame;
}
