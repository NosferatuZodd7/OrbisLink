// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The eyebrow: a line of small spaced capitals over a title — the section a
// page or a group belongs to. In champagne unless told otherwise.
import QtQuick

Text {
    color: Theme.brass
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontEyebrow
    font.weight: Font.DemiBold
    font.letterSpacing: Theme.eyebrowSpacing
    font.capitalization: Font.AllUppercase
    elide: Text.ElideRight
}
