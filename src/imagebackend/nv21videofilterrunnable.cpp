/**
 * @file nv21videofilterrunnable.h
 * @brief This class converts NV21 format to RGB888
 * @author Sergei Volodin
 * @version 1.0
 * @date 2018-07-26
 */

#include "nv21videofilterrunnable.h"
#include "nv21videofilter.h"
#include "qvideoframehelpers.h"
#include "timelogger.h"
#include <QImage>
#include <QOpenGLContext>
#include <qvideoframe.h>
#include <cstdio>
#include <QTextStream>

QImage imageWrapper(const QVideoFrame &frame)
{
    if (frame.handleType() == QAbstractVideoBuffer::GLTextureHandle) {
        // Slow and inefficient path. Ideally what's on the GPU should remain on the GPU, instead of readbacks like this.
        QImage img(frame.width(), frame.height(), QImage::Format_RGBA8888);
        GLuint textureId = frame.handle().toUInt();
        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        QOpenGLFunctions *f = ctx->functions();
        GLuint fbo;
        f->glGenFramebuffers(1, &fbo);
        GLuint prevFbo;
        f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *) &prevFbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);
        f->glReadPixels(0, 0, frame.width(), frame.height(), GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        f->glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        return img;
    } else
    {
        if (!frame.isReadable()) {
            qWarning("imageFromVideoFrame: No mapped image data available for read");
            return QImage();
        }

        QImage::Format fmt = QVideoFrame::imageFormatFromPixelFormat(frame.pixelFormat());
        if (fmt != QImage::Format_Invalid)
            return QImage(frame.bits(), frame.width(), frame.height(), fmt);

        qWarning("imageFromVideoFrame: No matching QImage format");
    }

    return QImage();
}


class QVideoFramePrivate : public QSharedData
{
public:
    QVideoFramePrivate()
        : startTime(-1)
        , endTime(-1)
        , mappedBytes(0)
        , planeCount(0)
        , pixelFormat(QVideoFrame::Format_Invalid)
        , fieldType(QVideoFrame::ProgressiveFrame)
        , buffer(0)
        , mappedCount(0)
    {
        memset(data, 0, sizeof(data));
        memset(bytesPerLine, 0, sizeof(bytesPerLine));
    }

    QVideoFramePrivate(const QSize &size, QVideoFrame::PixelFormat format)
        : size(size)
        , startTime(-1)
        , endTime(-1)
        , mappedBytes(0)
        , planeCount(0)
        , pixelFormat(format)
        , fieldType(QVideoFrame::ProgressiveFrame)
        , buffer(0)
        , mappedCount(0)
    {
        memset(data, 0, sizeof(data));
        memset(bytesPerLine, 0, sizeof(bytesPerLine));
    }

    ~QVideoFramePrivate()
    {
        if (buffer)
            buffer->release();
    }

    QSize size;
    qint64 startTime;
    qint64 endTime;
    uchar *data[4];
    int bytesPerLine[4];
    int mappedBytes;
    int planeCount;
    QVideoFrame::PixelFormat pixelFormat;
    QVideoFrame::FieldType fieldType;
    QAbstractVideoBuffer *buffer;
    int mappedCount;
    QMutex mapMutex;
    QVariantMap metadata;

private:
    Q_DISABLE_COPY(QVideoFramePrivate)
};


class TextureVideoBuffer : public QAbstractVideoBuffer
{
public:
    TextureVideoBuffer(uint id) : QAbstractVideoBuffer(GLTextureHandle), m_id(id) { }
    MapMode mapMode() const { return NotMapped; }
    uchar *map(MapMode, int *, int *) { return 0; }
    void unmap() { }
    QVariant handle() const { return QVariant::fromValue<uint>(m_id); }

private:
    GLuint m_id;
};


NV21VideoFilterRunnable::NV21VideoFilterRunnable(const NV21VideoFilterRunnable& backend) : QObject(nullptr)
{
    this->parent = (NV21VideoFilterRunnable*) &backend;
    gl = nullptr;
}

NV21VideoFilterRunnable::NV21VideoFilterRunnable(NV21VideoFilter *f) : filter(f), gl(nullptr), image_id(0)
{
    watcher.setParent(this);
    this->currentContext = QOpenGLContext::currentContext();

    // waiting for async output
    connect(&watcher, SIGNAL(finished()), this, SLOT(handleFinished()));
}

NV21VideoFilterRunnable::~NV21VideoFilterRunnable()
{
    if (gl != nullptr) {
        gl->glDeleteFramebuffers(1, &framebuffer);
        gl->glDeleteRenderbuffers(1, &renderbuffer);
    }
}

QVideoFrame NV21VideoFilterRunnable::run(QVideoFrame *inputFrame, const QVideoSurfaceFormat &surfaceFormat, QVideoFilterRunnable::RunFlags flags)
{
    Q_UNUSED(surfaceFormat); Q_UNUSED(flags);
    return run(inputFrame);
}

void NV21VideoFilterRunnable::convert() {
    QVideoFrame* inputFrame = parent->frame;
    QWindow* surf = new QWindow;
    surf->setSurfaceType(QWindow::OpenGLSurface);
    //Needs geometry to be a valid surface, but size is not important
    surf->setGeometry(-1, -1, 1, 1);
    surf->create();


    QOpenGLContext* context = new QOpenGLContext;
    context->setFormat(surf->requestedFormat());
    context->setShareContext(parent->currentContext);
    context->create();
    context->makeCurrent(surf);



    parent->image.save(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).append("/converted.png"));
}

void NV21VideoFilterRunnable::handleFinished()
{
    return;
    // sending image
    emit imageConverted(PipelineContainer<QImage>
                        (image, image_info.checkpointed("NV21")));

    TimeLoggerThroughput("%s", "[ANALYZE] Begin NV21VideoFilter");
}

struct QVF {
  typedef QVideoFramePrivate* QVideoFrame::*type;
  friend type get(QVF);
};


QVideoFrame NV21VideoFilterRunnable::run(QVideoFrame *inputFrame)
{
    TimeLoggerLog("%s", "NV21 start");

    auto size(inputFrame->size());
    auto height(size.height());
    auto width(size.width());

    auto outputHeight = height / 4;
    auto outputWidth(outputHeight * width / height);

    //TimeLoggerLog("%s", "NV21 auto OK");

    Q_ASSERT(inputFrame->handleType() == QAbstractVideoBuffer::HandleType::GLTextureHandle);

    //TimeLoggerLog("%s", "NV21 assert OK");

    if (gl == nullptr) {
        //TimeLoggerLog("%s", "NV21 need GL...");
        auto context(QOpenGLContext::currentContext());

        //TimeLoggerLog("%s %d", "NV21 Context OK", context);

        gl = context->extraFunctions();

        //TimeLoggerLog("%s", "NV21 extra OK");

        auto version(context->isOpenGLES() ? "#version 300 es\n" : "#version 130\n");

        auto sampleByPixelF(float(height) / float(outputHeight));
        unsigned int sampleByPixelI(std::ceil(sampleByPixelF));
        auto uvDelta(QVector2D(1, 1) / QVector2D(width, height));

        QString vertex(version);
        vertex += R"(
            out vec2 uvBase;
            void main(void) {
                int id = gl_VertexID;
                uvBase = vec2((id << 1) & 2, id & 2);
                gl_Position = vec4(uvBase * 2.0 - 1.0, 0.0, 1.0);
            }
        )";

        QString fragment(version);
        fragment += R"(
            in lowp vec2 uvBase;
            uniform sampler2D image;
            const uint sampleByPixel = %1u;
            const lowp vec2 uvDelta = vec2(%2, %3);
            out lowp float fragment;
                    lowp vec3 rgb2hsv(lowp vec3 c)
                    {
                        lowp vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
                        lowp vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
                        lowp vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

                        lowp float d = q.x - min(q.w, q.y);
                        lowp float e = 1.0e-10;
                        return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
                    }

            void main(void) {
                lowp vec3 sum = vec3(0.0, 0.0, 0.0);
                for (uint x = 0u; x < sampleByPixel; ++x) {
                    for (uint y = 0u; y < sampleByPixel; ++y) {
                        lowp vec2 uv = uvBase + vec2(x, y) * uvDelta;
                        sum += texture(image, uv).bgr;
                    }
                }
                lowp float divisor = float(sampleByPixel * sampleByPixel);
                lowp vec3 rgb = vec3(
                    sum.b / divisor,
                    sum.g / divisor,
                    sum.r / divisor);

                lowp vec3 hsv = rgb2hsv(rgb);

                    lowp float h = hsv.r;
                    lowp float s = hsv.g;
                    lowp float v = hsv.b;

                    lowp float diff;
                    lowp float mean_h_ = 0.0;
                    lowp float delta_h_ = 20.0 / 360.0;
                    lowp float min_s_ = 50. / 255.0;
                    lowp float max_s_ = 1.0;
                    lowp float min_v_ = min_s_;
                    lowp float max_v_ = 1.0;

                    if(h > 0.0) {
                        diff = h - mean_h_;
                    }
                    else {
                        diff = mean_h_ - h;
                    }

                    if(0.5 - diff < diff) {
                        diff = 0.5 - diff;
                    }

                    if(s >= min_s_ && s <= max_s_ &&
                            v >= min_v_ && v <= max_v_ &&
                            diff < delta_h_)
                    {
                        fragment = 1.0;
                    }
                    else fragment = 0.0;

                //fragment = rgb;
            }
        )";

        program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex);
        program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment.
                                        arg(sampleByPixelI).
                                        arg(uvDelta.x()).
                                        arg(uvDelta.y()));
        program.link();
        imageLocation = program.uniformLocation("image");

        gl->glGenRenderbuffers(1, &renderbuffer);
        gl->glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        gl->glRenderbufferStorage(GL_RENDERBUFFER, QOpenGLTexture::R8_UNorm, outputWidth, outputHeight);
        gl->glBindRenderbuffer(GL_RENDERBUFFER, 0);

        gl->glGenFramebuffers(1, &framebuffer);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Create one OpenGL texture
        gl->glGenTextures(1, &textureID);
        TimeLoggerLog("%s %d", "NV21 gentexture OK", textureID);
    }

    //TimeLoggerLog("%s", "NV21 context OK");

    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(QOpenGLTexture::Target2D, inputFrame->handle().toUInt());
    gl->glTexParameteri(QOpenGLTexture::Target2D, QOpenGLTexture::DirectionS, QOpenGLTexture::ClampToEdge);
    gl->glTexParameteri(QOpenGLTexture::Target2D, QOpenGLTexture::DirectionT, QOpenGLTexture::ClampToEdge);
    gl->glTexParameteri(QOpenGLTexture::Target2D, GL_TEXTURE_MIN_FILTER, QOpenGLTexture::Nearest);

    program.bind();
    program.setUniformValue(imageLocation, 0);
    program.enableAttributeArray(0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl->glViewport(0, 0, outputWidth, outputHeight);
    gl->glDisable(GL_BLEND);
    gl->glDrawArrays(GL_TRIANGLES, 0, 3);

    //TimeLoggerLog("%s", "NV21 convert OK"); // takes 60ms to the end from here, <1 from top.

    if(image.width() == 0) {
        image = QImage(outputWidth, outputHeight, QImage::Format_Grayscale8);
    }

    //TimeLoggerLog("%s", "NV21 image OK");

    gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);

    TimeLoggerLog("%s", "NV21 pixelStore OK");

    image_info = PipelineContainerInfo(image_id);
    image_id++;
    image_info.checkpoint("Grabbed");

    //gl->glActiveTexture(GL_TEXTURE0);
    //gl->glBindTexture(QOpenGLTexture::Target2D, inputFrame->handle().toUInt());



    // this call is very long
    //if(image_id % 5 == 0) {
        //gl->glReadPixels(0, 0, image.width(), image.height(), QOpenGLTexture::Red, QOpenGLTexture::UInt8, image.bits());
        //image.save(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).append("/converted.png"));
        //TimeLoggerLog("%s", "NV21 readPixels OK");
    //}

    // "Bind" the newly created texture : all future texture functions will modify this texture
    gl->glBindTexture(GL_TEXTURE_2D, textureID);
    gl->glCopyTexImage2D(GL_TEXTURE_2D, 0, QOpenGLTexture::R8_UNorm, 0, 0, outputWidth, outputHeight, 0);

    TimeLoggerLog("%s", "NV21 copyteximage OK");

    QVideoFramePrivate* priv = (QVideoFramePrivate*) inputFrame->d.data();

    qDebug() << "PRIV" << priv << priv->buffer << typeid(*(priv->buffer)).name() << typeid(priv).name();
    qDebug() << "PRIVPRIV" << priv->buffer->d_ptr; //0x00


    QVideoFrame frame = QVideoFrame(new TextureVideoBuffer(textureID),
                                    QSize(outputWidth, outputHeight),
                                    inputFrame->pixelFormat());
                                    //QVideoFrame::Format_Y8);
    //qDebug() << "MAP0" << frame.map(QAbstractVideoBuffer::ReadOnly);

    qDebug() << "MappedCount0" << inputFrame->d->mappedCount; // 0
    qDebug() << "MappedCount1" << frame.d->mappedCount; // 0
    //obj.toImage()
    int planes1 = inputFrame->d->buffer->mapPlanes(QAbstractVideoBuffer::ReadOnly, &inputFrame->d->mappedBytes,
                                                   inputFrame->d->bytesPerLine, inputFrame->d->data);
    qDebug() << "Planes1 " << planes1; // 0! because of the AndroidFilter... class TextureBuffer : public QAbstractVideoBuffer!!
//25AndroidTextureVideoBuffer!!

    int planes = frame.d->buffer->mapPlanes(QAbstractVideoBuffer::ReadOnly, &frame.d->mappedBytes, frame.d->bytesPerLine, frame.d->data);
    qDebug() << "Planes " << planes; // 0

    qDebug() << "PRIVPRIV1" << frame.d->buffer->d_ptr; // 0

    QImage res = imageWrapper(frame); // works in this thread

    //QImage res = QVideoFrameHelpers::VideoFrameBinToImage(frame);
    res.save(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).append("/converted.png"));
    TimeLoggerLog("%s", "NV21 frame OK");


    emit imageConverted(PipelineContainer<QImage>
                        (image, image_info.checkpointed("NV21")));

    return frame;

//    if(!watcher.isRunning()) {
//        image_info = PipelineContainerInfo(image_id);
//        image_id++;
//        QFuture<void> future = QtConcurrent::run(*this, &NV21VideoFilterRunnable::convert);
//        watcher.setFuture(future);
//    }

    //TimeLoggerLog("%s", "NV21 sent");

    return *inputFrame;
}