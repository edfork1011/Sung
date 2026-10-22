import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

MDialog {
    id: dialog
    objectName: "qqMusicLoginDialog"
    anchors.centerIn: parent
    width: fitWidth(440)
    height: fitHeight(implicitHeaderHeight + implicitFooterHeight + topPadding + bottomPadding + fields.implicitHeight + 8)
    title: "Sign in to QQ Music"
    modal: true
    standardButtons: Dialog.NoButton
    property string loginType: "qq"
    readonly property string scanner: loginType==="wx" ? "WeChat" : loginType==="mobile" ? "the QQ Music app" : "QQ on your phone"
    initialFocus: loginMethod
    onAboutToShow: app.startQQMusicLogin(loginType)
    onClosed: app.cancelQQMusicLogin()

    contentItem: ScrollView {
        id: scroll
        objectName: "qqMusicLoginScroll"
        clip: true
        contentWidth: availableWidth
        contentHeight: fields.implicitHeight
        rightPadding: 12
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: MScrollBar { parent: scroll; x: scroll.width-width; y: scroll.topPadding; height: scroll.availableHeight; orientation: Qt.Vertical }
        ColumnLayout {
            id: fields
            width: scroll.availableWidth
            spacing: 16
            MSegmentedControl {
                id: loginMethod
                objectName: "qqMusicLoginMethod"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                accessibleName: "QQ Music login method"
                options: [{key:"qq",label:"QQ",name:"qqLoginTypeQQ"},{key:"wx",label:"WeChat",name:"qqLoginTypeWX"},{key:"mobile",label:"QQ Music",name:"qqLoginTypeMobile"}]
                value: dialog.loginType
                onChosen: value=>{if(dialog.loginType!==value){dialog.loginType=value;app.startQQMusicLogin(value)}}
            }
            SungText {
                text: "Scan with " + dialog.scanner + ", then confirm the sign-in on your phone."
                Layout.fillWidth: true; wrapMode: Text.Wrap
                font.pixelSize: Theme.bodyMedium; color: Theme.muted
            }
            Rectangle {
                objectName: "qqMusicQRFrame"
                visible: !!app.qqMusicLoginArt
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.min(256, fields.width)
                Layout.preferredHeight: width
                color: "white"; radius: 12
                Image {
                    id: qrImage
                    objectName: "qqMusicQRCode"
                    anchors.fill: parent; anchors.margins: 16
                    source: app.qqMusicLoginArt
                    cache: false; smooth: false
                    fillMode: Image.PreserveAspectFit
                    Accessible.role: Accessible.Graphic
                    Accessible.name: "QQ Music sign-in QR code"
                }
            }
            MLoadingIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: app.qqMusicLoginStatus==="loading"
                label: "Getting a QQ Music QR code"
            }
            SungText {
                objectName: "qqMusicLoginStatus"
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: app.qqMusicLoginError || (qrImage.status===Image.Error ? "Could not display the QR code. Choose New code to try again." :
                    app.qqMusicLoginStatus==="loading" ? "Getting your QR code…" :
                    app.qqMusicLoginStatus==="waiting" ? "Waiting for you to scan…" :
                    app.qqMusicLoginStatus==="scanned" ? "Scanned. Confirm the sign-in on your phone." :
                    app.qqMusicLoginStatus==="done" ? "Signed in. You can now play songs your account can access." :
                    app.qqMusicLoginStatus==="expired" ? "This code has expired. Choose New code to try again." :
                    app.qqMusicLoginStatus==="refused" ? "Sign-in was declined on your phone." : "Choose a login method to continue.")
                color: app.qqMusicLoginError ? Theme.error : app.qqMusicLoginStatus==="done" ? Theme.primary : Theme.muted
                font.pixelSize: Theme.bodyMedium
            }
        }
    }
    scrollSource: scroll.contentItem
    footer: Item {
        implicitHeight: 88
        Row {
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 24; spacing: 8
            MButton { objectName: "qqMusicLoginClose"; text: app.qqMusicLoginStatus==="done" ? "Done" : "Cancel"; onClicked: dialog.close() }
            MButton {
                objectName: "qqMusicLoginRefresh"
                text: "New code"; filled: true
                visible: app.qqMusicLoginStatus!=="done"
                enabled: app.qqMusicLoginStatus!=="loading"
                onClicked: app.startQQMusicLogin(dialog.loginType)
            }
        }
    }
}
