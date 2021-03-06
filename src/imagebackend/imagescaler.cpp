/**
 * @file imagescaler.cpp
 * @brief This class scales images
 * @author Sergei Volodin
 * @version 1.0
 * @date 2018-07-25
 */

#include "timelogger.h"
#include "imagescaler.h"
#include "qvideoframehelpers.h"

ImageScaler::ImageScaler()
{
    watcher.setParent(this);
    connect(&watcher, SIGNAL(finished()), this, SLOT(handleResult()));
}

ImageScaler::ImageScaler(const ImageScaler& that)// : ImageScaler()
{
    this->target_width = that.target_width;
}

void ImageScaler::handleResult()
{
    buffer = watcher.result();

    emit imageAvailable(PipelineContainer<QImage>
                        (buffer, object_in_process.checkpointed("ImageScaler")));

    if(input_buffer_nonempty)
    {
        input_buffer_nonempty = false;
        setInput(input_buffer);
    }
}

QImage ImageScaler::scale(QImage source)
{
    // if no image is available, do nothing
    if(source.width() * source.height() <= 2 || target_width == 0)
        return QVideoFrameHelpers::empty();

    TimeLoggerThroughput("%s", "[ANALYZE] Begin Scale");

    QImage result = source.copy();

    // scaling it if necessary
    if(target_width < source.width())
        result = source.scaledToWidth(target_width);

    TimeLoggerThroughput("%s", "[ANALYZE] End Scale");

    return result;
}

void ImageScaler::setInput(PipelineContainer<QImage> source)
{
    input_buffer = source;

    if(!watcher.isRunning())
    {
        object_in_process = input_buffer.info();
        QFuture<QImage> future = QtConcurrent::run(*this, &ImageScaler::scale, source.o().copy());
        watcher.setFuture(future);
    }
    else input_buffer_nonempty = true;
}

ImageScaler::ImageScaler(int target_width) : ImageScaler()
{
    this->target_width = target_width;
}

QImage ImageScaler::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{ Q_UNUSED(id) Q_UNUSED(size) Q_UNUSED(requestedSize)
    return buffer;
}
