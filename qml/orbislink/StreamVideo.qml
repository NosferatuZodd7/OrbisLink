// SPDX-License-Identifier: AGPL-3.0-or-later
//
// O item de vídeo, num ficheiro só para ele.
//
// O "import QtMultimedia" fica isolado aqui de propósito: quando o
// OrbisLink é compilado sem Remote Play, ou quando o módulo QML do
// QtMultimedia não está instalado, este ficheiro nunca é carregado e o
// resto da janela abre na mesma. Ter o import no StreamArea.qml faria a
// interface inteira falhar a carregar nessas máquinas.
import QtQuick
import QtMultimedia

VideoOutput {
    id: video
    fillMode: VideoOutput.PreserveAspectFit

    // O teclado faz de comando enquanto houver stream.
    Keys.onPressed: function(event) {
        if (event.isAutoRepeat)
            return
        if (event.key === Qt.Key_Escape) {
            // Em ecrã inteiro, o Esc sai do ecrã inteiro; fora dele,
            // termina a sessão. É o que qualquer pessoa espera.
            if (window.streamFullscreen)
                window.setStreamFullscreen(false)
            else
                stream.stopStream()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_F11) {
            window.setStreamFullscreen(!window.streamFullscreen)
            event.accepted = true
            return
        }
        event.accepted = stream.keyPressed(event.key)
    }
    Keys.onReleased: function(event) {
        if (event.isAutoRepeat)
            return
        event.accepted = stream.keyReleased(event.key)
    }
    onActiveFocusChanged: if (!activeFocus) stream.releaseAllKeys()

    // O rato faz de touchpad: arrastar por cima da imagem é o mesmo que
    // arrastar o dedo no comando. Clicar é um toque.
    MouseArea {
        anchors.fill: parent
        enabled: stream.touchpadFromMouse()
        acceptedButtons: Qt.LeftButton
        cursorShape: enabled ? Qt.BlankCursor : Qt.ArrowCursor
        hoverEnabled: false

        function normalizado(ponto) {
            // O vídeo mantém a proporção, por isso a área desenhada pode ser
            // menor do que o item. Usa-se a área real, senão o toque saía
            // deslocado nas bandas pretas.
            var r = video.contentRect
            if (r.width <= 0 || r.height <= 0)
                return null
            var x = (ponto.x - r.x) / r.width
            var y = (ponto.y - r.y) / r.height
            if (x < 0 || x > 1 || y < 0 || y > 1)
                return null
            return { "x": x, "y": y }
        }

        onPressed: function(mouse) {
            var p = normalizado(mouse)
            if (p)
                stream.touchBegin(p.x, p.y)
            video.forceActiveFocus()
        }
        onPositionChanged: function(mouse) {
            var p = normalizado(mouse)
            if (p)
                stream.touchMove(p.x, p.y)
        }
        onReleased: stream.touchEnd()
        onCanceled: stream.touchEnd()
    }

    Component.onCompleted: stream.video.setVideoSink(video.videoSink)
}
