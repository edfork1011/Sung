import QtQuick
import QtQuick.Layouts

// ListItem.kt uses ListTokens.ItemLeadingSpace and ItemTrailingSpace (16dp)
// inside the interactive surface, so neither the label nor the trailing
// action touches the hover background's edges.
MButton {
    // A row that opens something says so with a trailing icon; a row that acts
    // immediately does not, so the two read differently at a glance.
    property bool opens: false
    trailingSymbol: opens ? "chevron" : ""
    leftAligned: true
    contentInset: 0
    leftPadding: 16
    rightPadding: 16
    // A list item's headline is body large (ListTokens.ItemLabelTextFont),
    // with body large's tracking and regular weight, not a button label's.
    labelSize: Theme.bodyLarge
    labelTypeRole: "bodyLarge"
    Layout.fillWidth: true
    Layout.minimumWidth: 0
}
