import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "qqMusicPlaylists"
    signal openRequested(var playlist)
    signal signInRequested()
    readonly property var rows: app.qqMusicPlaylists
    readonly property bool gridMode: app.viewMode==="grid" && height>=200
    readonly property var refreshTarget: grid.visible ? grid : list

    CatalogSkeleton { anchors.fill: parent; cards: true; loading: root.visible && app.busy && root.rows.length===0 }
    GridView {
        id: grid; objectName: "qqMusicPlaylistGrid"
        anchors.fill: parent; clip: true; reuseItems: true
        visible: root.gridMode
        model: root.visible ? root.rows : []
        cellWidth: width/Math.max(2,Math.floor(width/Theme.gridCell))
        cellHeight: cellWidth+56
        ScrollBar.vertical: MScrollBar {}
        MSmoothWheel { flick: grid }
        delegate: ArtCard {
            required property var modelData
            width: grid.cellWidth-16
            track: Object.assign({},modelData,{artist:modelData.playlistOrigin})
            openHandler: function(item) { root.openRequested(item) }
        }
        footer: Item {
            width: grid.width; height: app.canMore ? 56 : 0
            MButton { anchors.centerIn: parent; text: "Load more"; visible: app.canMore; busy: app.busy; enabled: !app.busy; onClicked: app.more() }
        }
    }
    ListView {
        id: list; objectName: "qqMusicPlaylistList"
        anchors.fill: parent; clip: true; spacing: 8; reuseItems: true
        visible: !root.gridMode
        model: root.visible ? root.rows : []
        ScrollBar.vertical: MScrollBar {}
        MSmoothWheel { flick: list }
        delegate: AbstractButton {
            id: playlistButton
            required property var modelData
            objectName: "qqMusicPlaylist_"+modelData.remoteId
            width: list.width; height: app.viewCompactDensity?68:80
            padding: 12; focusPolicy: Qt.StrongFocus; hoverEnabled: true
            Accessible.name: modelData.title+", "+modelData.playlistOrigin+", "+modelData.count+" songs"
            onClicked: root.openRequested(modelData)
            background: Rectangle {
                radius: Theme.shapeLarge; color: playlistButton.hovered ? Theme.high : Theme.container
                MFocusRing { targetRadius: Theme.shapeLarge; visible: playlistButton.visualFocus }
            }
            contentItem: RowLayout {
                spacing: 12
                Artwork { url: playlistButton.modelData.art || ""; Layout.preferredWidth: 48; Layout.preferredHeight: 48; radius: Theme.shapeMedium }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 4
                    SungText { text: playlistButton.modelData.title; Layout.fillWidth: true; font.pixelSize: Theme.bodyLarge }
                    SungText { text: playlistButton.modelData.playlistOrigin+" · "+playlistButton.modelData.count+" songs"+(playlistButton.modelData.artist?" · "+playlistButton.modelData.artist:""); Layout.fillWidth: true; color: Theme.muted; font.pixelSize: Theme.bodySmall }
                }
                Icon { name: "chevron"; size: 20; ink: Theme.muted }
            }
        }
        footer: Item {
            width: list.width; height: app.canMore ? 56 : 0
            MButton { anchors.centerIn: parent; text: "Load more"; visible: app.canMore; busy: app.busy; enabled: !app.busy; onClicked: app.more() }
        }
    }
    ColumnLayout {
        objectName: "qqMusicPlaylistsEmpty"
        anchors.centerIn: parent
        width: Math.min(360,parent.width-24); spacing: 12
        visible: !app.busy && root.rows.length===0
        SungText {
            text: !app.qqMusicCredential ? "Sign in to QQ Music to load your playlists" : app.error || app.qqMusicLibraryNotice ? "Couldn’t load your QQ Music playlists" : "No QQ Music playlists yet"
            Layout.fillWidth: true; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; color: Theme.muted
        }
        MButton {
            Layout.alignment: Qt.AlignHCenter
            text: !app.qqMusicCredential ? "Sign in" : "Refresh"
            tonal: true
            onClicked: { if(!app.qqMusicCredential)root.signInRequested();else app.refresh(); }
        }
    }
}
