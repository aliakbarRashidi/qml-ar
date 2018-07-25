/**
 * @file qtcamera2qml.h
 * @brief This is a wrapper over a QCamera which allows using it in QML
 * @author Sergei Volodin
 * @version 1.0
 * @date 2018-07-25
 */

#ifndef QTCAMERA2QML_H
#define QTCAMERA2QML_H

#include <QObject>
#include <QCamera>

/**
 * @brief This is a wrapper over a QCamera which allows using it in QML
 */

class QtCamera2QML : public QObject
{ Q_OBJECT
    Q_PROPERTY(QObject *mediaObject READ mediaObject NOTIFY never)
private:
    QCamera* camera;
public:
    QtCamera2QML(QCamera* cam);
    QObject* mediaObject();
signals:
    void never();
};

#endif // QTCAMERA2QML_H
