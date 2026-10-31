// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A four-pointed star, the atlas's mark: in the engraved rules, beside the
// page open in the navigation, wherever something is singled out.
import QtQuick
import QtQuick.Shapes

Shape {
    id: star
    property color color: Theme.brass
    property real size: 10

    width: size
    height: size
    // Multisampled, as the icons are: smooth curves on Qt 6.4 too.
    layer.enabled: true
    layer.samples: 4
    ShapePath {
        fillColor: star.color
        strokeColor: "transparent"
        startX: star.size / 2; startY: 0
        PathQuad { x: star.size; y: star.size / 2; controlX: star.size * 0.56; controlY: star.size * 0.44 }
        PathQuad { x: star.size / 2; y: star.size; controlX: star.size * 0.56; controlY: star.size * 0.56 }
        PathQuad { x: 0; y: star.size / 2; controlX: star.size * 0.44; controlY: star.size * 0.56 }
        PathQuad { x: star.size / 2; y: 0; controlX: star.size * 0.44; controlY: star.size * 0.44 }
    }
}
