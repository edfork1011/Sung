import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: header
    objectName: "qqMusicHomeHeader"
    spacing: 8
    signal signInRequested()
    signal searchRequested()
    signal customizeRequested()
    RowLayout {
        Layout.fillWidth: true; spacing: 12
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 2
            SungText { text: "QQ MUSIC"; font.pixelSize: Theme.labelLarge; labelRole: true; color: Theme.primary }
            SungText { objectName: "qqMusicHomeTitle"; text: "For you"; heading: true; emphasized: true; scaled: true; font.pixelSize: Theme.displaySmall; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }
        MAppBarRow {
            Layout.preferredWidth: Math.min(implicitWidth, header.width*0.55)
            Layout.preferredHeight: 48
            actions: [
                {key:"qqLogin",name:"qqMusicHomeLogin",text:app.qqMusicCredential?"Sign in again":"Sign in",label:"Sign in to QQ Music",trigger:function(){header.signInRequested()}},
                {key:"qqRefresh",name:"qqMusicHomeRefresh",symbol:"refresh",label:"Refresh recommendations",enabled:!app.busy,trigger:function(){app.home()}},
                {key:"qqSearch",name:"qqMusicHomeSearch",symbol:"search",label:"Search QQ Music",trigger:function(){header.searchRequested()}},
                {key:"qqCustomize",name:"editHomeButton",symbol:"settings",label:"Customize Home",trigger:function(){header.customizeRequested()}}
            ]
        }
    }
    SungText {
        objectName: "qqMusicHomeSubtitle"
        text: "Discover songs, playlists and new releases from QQ Music."
        visible: header.width>=600
        Layout.fillWidth: true; wrapMode: Text.Wrap
        font.pixelSize: Theme.bodyMedium; color: Theme.muted
    }
    SungText {
        objectName: "qqMusicHomeNotice"
        visible: !!app.homeNotice; text: app.homeNotice
        Layout.fillWidth: true; wrapMode: Text.Wrap
        font.pixelSize: Theme.bodySmall; color: Theme.muted
    }
}
