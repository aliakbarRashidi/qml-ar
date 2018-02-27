#include <QVariant>
#include <QtQml>
#include <QCameraInfo>
#include "qmlar.h"
#include "imageproviderasync.h"
#include "uchiyamarkerdetector.h"
#include "imagebackend.h"
#include "perspectivecamera.h"
#include "portablebackendfactory.h"
#include "markerbackend.h"
#include "markermvpprovider.h"
#include "calibratedcamerafilestorage.h"
#include "imumvpdecorator.h"
#include "posepredictor.h"
#include "trackingdecorator.h"
#include "blobdetector.h"
#include "qtcamera2qml.h"
#include "imagescaler.h"
#include "markerstorage.h"
#include "hsvintervaldetector.h"
#include "huethreshold.h"
#include "qtbackend.h"
#include "timelogger.h"
#include "qml-imu/src/IMU.h"
#include "qvector3d.h"
#include "linearposepredictor.h"
#include "randomimagebackend.h"
#include "qtcamera2qml.h"
#include "worldimage.h"
#include "fpscalculator.h"
#include "posefilter.h"

QMLAR::QMLAR()
{
    // initially, object is not initialized
    is_initialized = false;
    camera_id = -2;
    image_filename = "";
    raw_provider = NULL;
    perspective_camera = NULL;
    camera_wrapper = NULL;
    marker_backend = new MarkerBackEnd();
    marker_storage = new MarkerStorage();

    // reserve threads
    for(int i = 0; i < 12; i++)
        threads.append(new QThread());

    // allowing up to 10 parallel tasks
    // (separate from previous call)
    QThreadPool::globalInstance()->setMaxThreadCount(10);

    // no fps calculator yet
    fps = NULL;
}

int QMLAR::getCameraId()
{
    Q_ASSERT(is_initialized);
    return camera_id;
}

void QMLAR::setCameraId(int camera_id)
{
    Q_ASSERT(!is_initialized);
    TimeLoggerLog("Using camera %d", camera_id);
    this->camera_id = camera_id;

    // Random provider (debug)
    //init_type = INIT_IMAGE;
    //raw_provider = new RandomImageBackend();

    // camera
    raw_provider = PortableCameraBackendFactory::getBackend(camera_id);
    init_type = INIT_CAMERA;

    initialize();
}

void QMLAR::setImageFilename(QString filename)
{
    Q_ASSERT(!is_initialized);
    TimeLoggerLog("Opening image %s", filename.toStdString().c_str());
    image_filename = filename;
    raw_provider = new ImageBackend(filename);
    init_type = INIT_IMAGE;
    initialize();
}

QMatrix4x4 QMLAR::getMVPMatrix()
{
    return mvp_buffer;
}

int QMLAR::getImageWidth()
{
    init_sem.acquire();
    if(perspective_camera == NULL)
        return image_width;
    else
        return perspective_camera->width();
}

void QMLAR::setImageWidth(int new_width)
{
    Q_ASSERT(!is_initialized);
    image_width = new_width;
}

QQuickImageProvider *QMLAR::getImageProvider()
{
    return marker_backend;
}

QObject* QMLAR::getCamera()
{
    init_sem.acquire();
    if(init_type == INIT_CAMERA && PortableCameraBackendFactory::cameraViewfinderAvailable())
    {
        QCamera* camera = dynamic_cast<QtCameraBackend*>(raw_provider)->getCamera();
        if(camera_wrapper == NULL)
            camera_wrapper = new QtCamera2QML(camera);
        TimeLoggerLog("%s", "Returning camera object");
        return camera_wrapper;
    }
    else
    {
        TimeLoggerLog("%s", "No camera in this mode");
        return NULL;
    }
}

QVariantList QMLAR::getBlobs()
{
    QVariantList result;
    QVector<QVector2D>::iterator it;
    for(it = last_blobs.begin(); it != last_blobs.end(); it++)
    {
        result << *it;
    }
    return result;
}

void QMLAR::setDots(QPair<QImage, QVector<QVector2D> > image_dots)
{
    setBlobs(image_dots.second);
}

QVariantList QMLAR::getMarkers()
{
    QVariantList result{};

    QMap<int, Marker>::iterator it;
    for(it = marker_storage->begin(); it != marker_storage->end(); it++)
    {
        WorldImageCorrespondences c = it.value().getCorrespondences();
        if(c.size() != 4) continue;
        // positioning (mm)    (0, 0)    (0, s)       (s, 0)     (s, s)
        result << c.getImagePoint(0).toVector2D();
        result << c.getImagePoint(1).toVector2D();
        result << c.getImagePoint(3).toVector2D();
        result << c.getImagePoint(2).toVector2D();
    }

    return result;
}

double QMLAR::getFPSMean()
{
    if(fps)
        return fps->mean();
    return 0;
}

double QMLAR::getFPSStd()
{
    if(fps)
        return fps->std();
    return 0;
}

void QMLAR::startCamera()
{
    init_sem.acquire();
    if(init_type == INIT_CAMERA && PortableCameraBackendFactory::cameraViewfinderAvailable())
    {
        TimeLoggerLog("%s", "Starting camera");
        dynamic_cast<QtCameraBackend*>(raw_provider)->start();
    }
}

void QMLAR::setMVP(QMatrix4x4 mvp)
{
    mvp_buffer = mvp;
    emit newMVPMatrix(mvp_buffer);
}

void QMLAR::setMV(QMatrix4x4 mv)
{
    mv_buffer = mv;
    emit newMVMatrix(mv_buffer);
}

void QMLAR::setP(QMatrix4x4 p)
{
    p_buffer = p;
    emit newPMatrix(p_buffer);
}

void QMLAR::setBlobs(QVector<QVector2D> blobs)
{
    last_blobs = blobs;
    emit newBlobs(getBlobs());
}

void QMLAR::setMarkers(MarkerStorage storage)
{
    *marker_storage = storage;
    emit newMarkers(getMarkers());
}

void QMLAR::hueAvailable(double mean, double std)
{ Q_UNUSED(mean) Q_UNUSED(std)
    // severing input to hsv_interval after first available result

    // X HSV -> thresholding
    disconnect(detector, &UchiyaMarkerDetector::dotsFound, hsv_interval, &HSVIntervalDetector::newPoints);

    // X tracking -> blobs
    disconnect(tracking, &TrackingDecorator::imageAvailable, blob_detector, &BlobDetector::setInput);

    // X blobs -> markers
    disconnect(blob_detector, &BlobDetector::imageAvailable, detector, &UchiyaMarkerDetector::setInput);

    // tracking -> hsv
    connect(tracking, &TrackingDecorator::imageAvailable, hue_threshold, &HueThreshold::setInput);

    // hsv -> detector
    //connect(hue_threshold, &HueThreshold::imageAvailable, detector, &UchiyaMarkerDetector::setInput);
}

void QMLAR::connectAll()
{
    hue_threshold->setColor(0, 20);
    qRegisterMetaType<QPair<QImage, QVector<QVector2D>>>("QPair<QImage, QVector<QVector2D>>");
    qRegisterMetaType<QVector<QVector2D>>("QVector<QVector2D>");
    qRegisterMetaType<MarkerStorage>("MarkerStorage");

    connect(hue_threshold, &HueThreshold::imageAvailable, marker_backend, &MarkerBackEnd::setPreview);
    connect(hue_threshold, &HueThreshold::imageAvailable, this, &QMLAR::imageUpdated);
    connect(mvp_provider, &MarkerMVPProvider::newMVPMatrix, fps, &FPSCalculator::newFrame);

    //connect(mvp_provider, &MarkerMVPProvider::newMVPMatrix, this, &QMLAR::setMVP);

    // camera -> scaler
    connect(raw_provider, &ImageProviderAsync::imageAvailable, scaler, &ImageScaler::setInput);

    // camera -> QML
    connect(scaler, &ImageScaler::imageAvailable, marker_backend, &MarkerBackEnd::setCamera);

    // scaler -> tracking
    connect(scaler, &ImageScaler::imageAvailable, tracking, &TrackingDecorator::setInput);

    // scaler -> resolution
    connect(scaler, &ImageScaler::imageAvailable, perspective_camera, &CalibratedCamera::setResolutionImage);

//    // tracking -> blobs
//    connect(tracking, &TrackingDecorator::imageAvailable, blob_detector, &BlobDetector::setInput);

    // tracking -> threshold
    connect(tracking, &TrackingDecorator::imageAvailable, hue_threshold, &HueThreshold::setInput);

    connect(hue_threshold, &HueThreshold::imageAvailable, detector, &UchiyaMarkerDetector::setInput);

    // blobs -> markers
    connect(blob_detector, &BlobDetector::imageAvailable, detector, &UchiyaMarkerDetector::setInput);

//    // blobs -> QML
//    //connect(blob_detector, &BlobDetector::blobsUpdated, this, &QMLAR::setBlobs);

    // blobs -> QML (from Uchiya)
    connect(detector, &UchiyaMarkerDetector::dotsAll, this, &QMLAR::setBlobs, Qt::QueuedConnection);

    // markers -> QML
//    //connect(detector, &UchiyaMarkerDetector::previewUpdated, marker_backend, &MarkerBackEnd::setPreview);
    connect(detector, &UchiyaMarkerDetector::markersUpdated, this, &QMLAR::setMarkers);

//    // threshold -> QML
//    connect(hue_threshold, &HueThreshold::imageAvailable, marker_backend, &MarkerBackEnd::setPreview);

    // markers -> MVP
    connect(detector, &UchiyaMarkerDetector::markersUpdated, mvp_provider, &MarkerMVPProvider::recompute);

    // markers -> tracking
    connect(detector, &UchiyaMarkerDetector::markersUpdated, tracking, &TrackingDecorator::onNewMarkers);

//    // markers -> HSV detector
//    connect(detector, &UchiyaMarkerDetector::dotsFound, hsv_interval, &HSVIntervalDetector::newPoints, Qt::QueuedConnection);

//    // HSV -> this
//    connect(hsv_interval, &HSVIntervalDetector::hAvailable, this, &QMLAR::hueAvailable);

//    // HSV -> thresholding
//    connect(hsv_interval, &HSVIntervalDetector::hAvailable, hue_threshold, &HueThreshold::setColor);
//    connect(hsv_interval, &HSVIntervalDetector::sAvailable, hue_threshold, &HueThreshold::setS);
//    connect(hsv_interval, &HSVIntervalDetector::vAvailable, hue_threshold, &HueThreshold::setV);

//    // mvp -> tracking
    connect(mvp_provider, &MarkerMVPProvider::newMVMatrix, tracking, &TrackingDecorator::onNewMVMatrix);
    connect(mvp_provider, &MarkerMVPProvider::newPMatrix, tracking, &TrackingDecorator::onNewPMatrix);

//    // mvp -> FPS
//    connect(detector, &UchiyaMarkerDetector::markersUpdated, this, &QMLAR::imageUpdated);

    // mvp -> imu
    connect(mvp_provider, &MarkerMVPProvider::newMVMatrix, pose_filter, &PoseFilter::setMV);
    connect(pose_filter, &PoseFilter::newMVMatrix, mvp_imu_decorated, &IMUMVPDecorator::setMV);

    connect(mvp_provider, &MarkerMVPProvider::newPMatrix, mvp_imu_decorated, &IMUMVPDecorator::setP);
    connect(mvp_provider, &MarkerMVPProvider::newPMatrix, pose_filter, &PoseFilter::setP);

//    // output MVP matrix from IMU decorator
    connect(mvp_imu_decorated, &IMUMVPDecorator::newMVPMatrix, this, &QMLAR::setMVP);
    connect(mvp_imu_decorated, &IMUMVPDecorator::newMVMatrix, this, &QMLAR::setMV);
    connect(mvp_imu_decorated, &IMUMVPDecorator::newPMatrix, this, &QMLAR::setP);

    connect(this, &QMLAR::newFilterAlpha, pose_filter, &PoseFilter::setAlpha);
}

QString QMLAR::getImageFilename()
{
    Q_ASSERT(is_initialized);
    return image_filename;
}

void QMLAR::initialize()
{
    // sanity check
    Q_ASSERT(raw_provider != NULL);

    // creating Uchiya marker detector
    detector = new UchiyaMarkerDetector;

    // setting up assets path (os-dependent)
#ifdef Q_OS_ANDROID
    QString ASSETS_PATH = "assets:/";
#else
    QString ASSETS_PATH = ":/assets/";
#endif

    // creating blob detector
    blob_detector = new BlobDetector(max_dots);

    // loading marker positions
    detector->loadMarkerPositions(ASSETS_PATH + "markers.json");

    // connecting to IMU
    imu = new IMU();

    // setting Accelerometer bias (TODO: fix hardcode)
    imu->setProperty("accBias", QVector3D(0.397, -0.008, -0.005));

    // loading camera matrix
    camera_matrix = new CalibratedCameraFileStorage(ASSETS_PATH + "camera_matrix.json");

    // decorating camera matrix object
    // allowing to obtain perspective matrix
    perspective_camera = new PerspectiveCamera(camera_matrix);

    // creating a ModelView provider
    mvp_provider = new MarkerMVPProvider(perspective_camera);

    // creating linear pose predictor
    predictor = new LinearPosePredictor();

    // adding tracking to marker detector
    tracking = new TrackingDecorator(predictor);

    // decorating MVP with IMU
    mvp_imu_decorated = new IMUMVPDecorator(imu);

    // creating image scaler
    scaler = new ImageScaler(image_width);

    // creating HSV interval detector
    hsv_interval = new HSVIntervalDetector(1000);

    // creating hsv thresholder
    hue_threshold = new HueThreshold();

    // creating pose filter
    pose_filter = new PoseFilter(0.5);

    // calculating mean/std fps based on 100 calls
    fps = new FPSCalculator(100);

    // moving objects to threads
    // index in threads array
    int thread_to_use = 0;
    imu->moveToThread(threads[thread_to_use++]);
    detector->moveToThread(threads[thread_to_use++]);
    blob_detector->moveToThread(threads[thread_to_use++]);
    mvp_provider->moveToThread(threads[thread_to_use++]);
    tracking->moveToThread(threads[thread_to_use++]);
    mvp_imu_decorated->moveToThread(threads[thread_to_use++]);
    scaler->moveToThread(threads[thread_to_use++]);
    hsv_interval->moveToThread(threads[thread_to_use++]);
    hue_threshold->moveToThread(threads[thread_to_use++]);
    raw_provider->moveToThread(threads[thread_to_use++]);
    fps->moveToThread(threads[thread_to_use++]);
    pose_filter->moveToThread(threads[thread_to_use++]);

    // connecting everything
    connectAll();

    // start queue threads
    for(int i = 0; i < thread_to_use; i++)
        threads[i]->start();

    // now the object is initialized
    is_initialized = true;

    // 10 functions can know that object is initialized
    init_sem.release(10);
}
