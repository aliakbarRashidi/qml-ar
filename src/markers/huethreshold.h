/**
 * @file huethreshold.h
 * @brief This class outputs binarized image
 * taking colors in [hue-delta, hue+delta]
 * @author Sergei Volodin
 * @version 1.0
 * @date 2018-07-25
 */

#ifndef HUETHRESHOLD_H
#define HUETHRESHOLD_H

#include "imageproviderasync.h"
#include "opencv2/highgui.hpp"
#include <QtConcurrent>

/**
 * @brief This class outputs binarized image
 * taking colors in [hue-delta, hue+delta]
 */

#define IMAGE_MAX_PIXELS (640 * 480)

class HueThreshold : public ImageProviderAsync
{ Q_OBJECT
public:
    HueThreshold();
    virtual ~HueThreshold() {}
    HueThreshold(const HueThreshold &that);

private:
    /**
    * @brief Input id
    */
    PipelineContainerInfo object_in_process;

    /**
    * @brief Buffers
    */
    cv::Mat img;
    cv::Mat hsv;
    cv::Mat result;
    cv::Mat result_rgb;
    QImage result_qt;
    cv::Mat mask;

    uchar buf[IMAGE_MAX_PIXELS];

    /**
    * @brief For results from thread
    */
    QFutureWatcher<QImage> watcher;

    /**
    * @brief Minimum and maximum values for thresholding
    */
    QVector<cv::Scalar> min_hsv;
    QVector<cv::Scalar> max_hsv;

    // min/max hue
    int mean_h, delta_h;

    /**
    * @brief True if image is pending processing
    */
    int input_buffer_nonempty;

    /**
    * @brief Buffer for input images
    */
    PipelineContainer<QImage> input_buffer;

    /**
    * @brief Minimal and maximal SV values
    */
    int min_s, max_s, min_v, max_v;

    // set min/max hue values in 0..360
    void addMaxHue(double hue);
    void addMinHue(double hue);
public slots:
    /**
    * @brief Set color to threshold on
    */
    void setColor(double mean, double std);

    /**
    * @brief Set V, S distribution
    */
    void setV(double mean, double std);
    void setS(double mean, double std);

    /**
    * @brief Set input image
    */
    void setInput(PipelineContainer<QImage> input);

    /**
    * @brief Handle result from thread
    */
    void handleFinished();

    /**
    * @brief Loop implementation
    */
    QImage thresholdManual(QImage source);

    /**
    * @brief CV implementation
    */
    QImage threshold(QImage source);
    void setVMinMax(double min_, double max_);
    void setSMinMax(double min_, double max_);
};

#endif // HUETHRESHOLD_H
