/**
 * @file marker.h
 * @brief This class holds the position in the sheet
 * of a single marker
 * @author Sergei Volodin
 * @version 1.0
 * @date 2018-07-25
 */

#ifndef MARKER_H
#define MARKER_H

#include <QVector2D>
#include <QString>
#include <QMatrix4x4>
#include <QJsonObject>
#include "worldimage.h"

/**
 * @brief This class holds the position in the sheet
 * of a single marker
 *
 * coordinates: top-left corner
 *
 * INPUT/OUTPUT COORDINATE SYSTEM (OpenCV/QML):
 * x right
 * y down
 * z from reader
 */

class Marker
{
private:
    /**
    * @brief Number of the marker
    */
    int id;

    /**
    * @brief Name of the marker
    */
    QString name;

    /**
    * @brief Postition of the marker in the activity sheet
    * Top-left corner, in millimetres
    */
    QVector2D position_mm;

    /**
    * @brief Size of the single side in mm
    */
    double size_mm;

    /**
    * @brief Correspondences 2D -> 3D
    */
    WorldImageCorrespondences correspondences;

    /**
    * @brief Configuration of a marker
    */
    QJsonObject config;

    void setPositionMM(QVector2D position_mm_);
public:
    Marker();

    /**
     * @brief Marker constructor
     * @param x_mm x position in mm
     * @param y_mm y position in mm
     */
    Marker(double x_mm, double y_mm);

    /**
     * @brief Marker constructor
     * @param position_mm_ position vector (x, y) in mm
     */
    Marker(QVector2D position_mm_);

    /**
    * @brief Set name of the marker
    */
    void setName(QString name_);

    /**
     * @brief Set ID of the marker
     */
    void setId(int id_);

    /**
     * @brief Set size of the marker square in mm
     */
    void setSizeMM(double size_);

    /**
     * @brief Set marker configuration
     */
    void setConfig(const QJsonObject value);

    /**
    * @brief Get position of top-left corner in the world
    * QML coordinate system
    */
    const QVector2D getPositionMM();

    /**
    * @brief Size in millimeters of a single edge
    */
    double getSizeMM();

    /**
    * @brief Add single point to correspondences 3D-2D
    */
    void addCorrespondence(QVector3D world_point, QVector2D image_point);

    /**
    * @brief Return 3D-2D correspondences
    */
    WorldImageCorrespondences& getCorrespondences();

    /**
    * @brief Return config of the marker
    */
    QJsonObject getConfig() const;

    /**
    * @brief Forget marker being detected
    */
    void undetect();

    /**
     * @brief Get string description of a marker
     * @return String representation
     */
    QString toString();
};

#endif // MARKER_H
