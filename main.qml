import QtQuick 2.8
import QtQuick.Window 2.2

Window
{
    visible: true
    width: 640
    height: 480
    title: qsTr("Hello World")

    Rectangle
    {
        objectName: "frameboy"
        width: 400
        height: 300
        anchors.centerIn: parent

        color: "green"

        Rectangle
        {
            id: r
            color: "red"
            width: 50
            height: 100
            anchors.centerIn: parent

            Timer
            {
                interval: 50
                repeat: true
                running: true
                onTriggered:
                {
                    r.rotation += 5
                }
            }
        }
    }


}
