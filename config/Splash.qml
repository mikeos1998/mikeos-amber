import QtQuick 2.5

Rectangle {
    id: root
    color: "#f08a00"
    property int stage

    Image {
        id: logo
        anchors.centerIn: parent
        source: "images/logo.svg"
        sourceSize.width: 256
        sourceSize.height: 256
    }
    Text {
        anchors.top: logo.bottom
        anchors.topMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        text: "Mike OS: Amber"
        color: "#1f1a1a"
        font.pixelSize: 32
        font.bold: true
    }
}
