/**
 * @file AR3DScene.qml
 * @brief This component defines the camera settings and loads the user-defined 3D scene
 * @author Sergei Volodin
 * @version 1.0
 * @date 2018-08-02
 */

import QtQuick 2.6
import ch.epfl.chili.qmlar 1.0
import QtQuick.Scene3D 2.0

import Qt3D.Core 2.0
import Qt3D.Render 2.0
import Qt3D.Input 2.0
import Qt3D.Extras 2.0

/** @brief This component defines the camera settings and loads the user-defined 3D scene */
Scene3D {
    id: scene3d
    //anchors.fill: parent
    //anchors.margins: 0
    focus: true
    visible: true
    aspects: ["input", "logic"]
    cameraAspectRatioMode: Scene3D.AutomaticAspectRatio

    /** Set this to the actual surface with 3D models.
     * Will create an object of this component.
     * and add this to 3D scene. */
    property var arSceneComponent

    /** parameters for the arScene */
    property var arSceneParameters

    /** the resulting object will be stored here */
    property var arSceneObject

    /** delta MVP x axis (in mm?) */
    property double delta_x: 0

    /** Speed up the rendering (if false) @see https://forum.qt.io/topic/84698/why-is-scene3d-so-slow/2 */
    multisample: false

    // entity with camera which creates user's components inside
    Entity {
        id: activity

        // add camera as a component
        components: [
            RenderSettings {
                activeFrameGraph: ForwardRenderer {
                    camera: camera
                    clearColor: "transparent"
                }
            },
            InputSettings { }
        ]

        // set ModelViewProjection matrix as camera matrix
        Camera {
            id: camera

            // pose valid -> P * Delta_X * MV
            // pose invalid -> zero
            projectionMatrix: AR.pose_valid ?
                                  AR.p_matrix.times(Qt.matrix4x4(1, 0, 0, delta_x,
                                                                 0, 1, 0, 0,
                                                                 0, 0, 1, 0,
                                                                 0, 0, 0, 1)).times(AR.mv_matrix)
                                : Qt.matrix4x4(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        }

        // load scene on component loading
        Component.onCompleted: {
            console.log("Begin loading scene " + arSceneComponent + " with params " + arSceneParameters);
            if(!arSceneComponent) {
                console.log("No scene found (null object)");
            }
            else {
                if(arSceneComponent.status === Component.Error) {
                    console.debug("Error loading scene: " + arSceneComponent.errorString());
                }
                else {
                    arSceneObject = arSceneComponent.createObject(activity, arSceneParameters);
                }
            }
            console.log("End loading scene");
        }
    }
}
