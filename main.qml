import QtQuick 2.7
import QtQuick.Controls 1.4
import QtQuick.Layouts 1.1
import QtQuick.Dialogs 1.2

Rectangle {
    id: root;
    width: 780;
    height: 570;

    property string announceurls;
    property string webseedurls;
    property string lastsavepath;
    property bool announcemultitier;
    property bool busy: false;


    function fileSizeIEC(a,b,c,d,e){
     return  (b=Math,c=b.log,d=1024,e=c(a)/c(d)|0,a/b.pow(d,e)).toFixed(2).replace(".00", "")
     +' '+(e?'KMGTPEZY'[--e]+'iB':'Bytes');
    }

    function updateUI() {
        var contentlength = torrent.getContentLength();
        footertext0.text = "Pieces: #: " + torrent.getPieceNumber() + " S: " + fileSizeIEC(torrent.getPieceLength());
        footertext1.text = "Total size: " + fileSizeIEC(contentlength);
        footertext2.text = "Metainfo size: " + fileSizeIEC(torrent.calculateTorrentfileSize());
    }

    Timer {
        id: etatimer;
        repeat: true;
        property int elapsed: 0;
        onTriggered: elapsed++;

        function getETA(percentage) {
            return (new Date((((100 / percentage) * elapsed) - elapsed) * 1000)).toUTCString().match(/(\d\d:\d\d:\d\d)/)[0];
        }
    }

    MessageDialog {
        id: findiag;
        modality: Qt.ApplicationModal;
        title: "Operation finished";
    }

    MessageDialog {
        id: reloaddiag;
        modality: Qt.ApplicationModal;
        title: "Oops";
        text: "Files in the directory you have chosen where added or removed. Do you want to reload the fileslist?";
        standardButtons: StandardButton.Yes | StandardButton.No;
        onYes: torrent.setDirectory(torrent.getParentDirectory + "/" + torrent.getRealName());
    }

    Connections {
        target: torrent;
        onProgress: {
            progresstext.text = percentage + "%" + " ETA: " + etatimer.getETA(percentage);
            progressbar.value = percentage;
        }
        onFinished: {
            findiag.title = success ? "Operation finished" : "Error";
            findiag.text = success ? "Creation complete.\nInfo hash: " + torrent.getInfoHash(true) : "Something went wrong.";
            findiag.open();
            etatimer.stop();
            busy = false;
        }
        onWatchedDirChanged: {if (!reloaddiag.visible) reloaddiag.open();}
        onError: {
            findiag.title = "Error";
            findiag.text = msg;
            findiag.open();
            busy = false;
        }
    }


    Rectangle {
        id: help;
        anchors {centerIn: parent;}
        border.color: "grey";
        border.width: 5;
        radius: 10;
        color: "lightgrey";
        z: 50;

        property var texts: ({});

        Component.onCompleted: {
            texts.name = "For single file torrents this is the name of the file.\nFor multi-file torrents this is the name of the directory the files will be in.\n\nWhen changing this be advised that your torrent client will look for a file / folder with this name.\nThis means you have to either change it in your client as well or rename your file / folder for your client to find it.";
            texts.announce = "The tracker annouce url(s).\nSeperate multiple urls with a new line.\n\nBy default each url will have it's own tier. If you want to have them all in 1 tier add:\nAnnouncemultitier=false\n to the config file.";
            texts.webseed = "The webseed url(s).\n Seperate multiple urls with a new line.";
            texts.comment = "Pretty obvious, isn't it?";
            texts.additional = "Here you can set any data you want inside your metainfo file.\nData has to be a key value pair seperated by '='.\nIf you want to add multiple pairs seperate them with a new line.\n\nExample:\nkey1=value1\nkey2=value2";
            texts.private = "Sets this torrent to be private.\nThis means (as long as the client honors it) no DHT or PeX will be used to spread the torrent.";
            texts.piecelength = "Smaller piece lengths will make it easier to spread the torrent but increase the size of the .torrent file.\nIf you are unsure what to use just leave it to be \"Automatic\".\n16MiB is the highest supported by all torrent clients, higher values might not be supported by some clients.";
            texts.savepath = "Filename for the output metafile.";
        }

        function show(text) {
            helptext.text = text;
            width = parent.width - 20;
            height = parent.height - 20;
        }

        Behavior on width {NumberAnimation {duration: 200;}}
        Behavior on height {NumberAnimation {duration: 200;}}

        MouseArea {anchors.fill: parent; onClicked: {parent.width = 0; parent.height = 0;}}

        Text {
            id: helptext;
            anchors {fill: parent; margins: 15;}
            horizontalAlignment: Qt.AlignHCenter;
            verticalAlignment: Qt.AlignVCenter;
            wrapMode: Text.WordWrap;
            font.pixelSize: 20;
            visible: parent.width > root.width /2 ? true : false;
        }
    }

    RowLayout {
        id: header;
        anchors {left: parent.left; right: parent.right; top: parent.top; margins: 5;}
        height: 50;

        Button {
            enabled: !busy;
            text: "  From File";
            implicitHeight: 40;
            implicitWidth: parent.width /4;
            Layout.alignment: Qt.AlignCenter;
            iconSource: "qrc:/images/file.ico";
            FileDialog {
                id: fdfile;
                title: "Choose a file";
                onAccepted: {
                    torrent.setFile(fileUrl.toString().substring(7));
                    namefield.text = torrent.getName();
                    piecelengthcombo.currentIndex = -1;
                    piecelengthcombo.currentIndex = 0;
                    if (lastsavepath)
                        savepathedit.text = lastsavepath + torrent.getName().replace(/[^\.]*$/, "torrent");
                }
            }
            onClicked: fdfile.open();
        }

        Button {
            enabled: !busy;
            text: "  From Directory";
            implicitHeight: 40;
            implicitWidth: parent.width /4;
            Layout.alignment: Qt.AlignCenter;
            iconSource: "qrc:/images/dir.png";
            FileDialog {
                id: fddir;
                title: "Choose a directory";
                selectFolder: true;
                onAccepted: {
                    torrent.setDirectory(fileUrl.toString().substring(7));
                    namefield.text = torrent.getName();
                    piecelengthcombo.currentIndex = -1;
                    piecelengthcombo.currentIndex = 0;
                    if (lastsavepath)
                        savepathedit.text = lastsavepath + torrent.getName() + ".torrent";
                }
            }
            onClicked: fddir.open();
        }

        Button {
            enabled: !busy;
            text: "  Load .torrent";
            implicitHeight: 40;
            implicitWidth: parent.width /4;
            Layout.alignment: Qt.AlignCenter;
            iconSource: "qrc:/images/load.png";
            FileDialog {
                id: fdloadtor;
                title: "Choose a file";
                nameFilters: ["Torrent file (*.torrent)"];
                onAccepted: {
                    torrent.load(fileUrl.toString().substring(7));
                    piecelengthcombo.currentIndex = piecelengthcombo.find(fileSizeIEC(torrent.getPieceLength()));
                    namefield.text = torrent.getName();
                    announce.text = torrent.getAnnounceUrls().join('\n');
                    if (torrent.getWebseedUrls().join('\n').length > 1)
                        webseed.text = torrent.getWebseedUrls().join('\n');
                    if (torrent.getComment())
                        commentfield.text = torrent.getComment();
                    privatecheck.checked = torrent.isPrivate();
                }
            }
            FileDialog {
                id: fdrootdir;
                title: "Choose the directory where the file or directory is in";
                selectFolder: true;
                onAccepted: {
                    torrent.setRootDirectory(fileUrl.toString().substring(7));
                }
            }

            onClicked: {
                fdrootdir.open();
                fdloadtor.open();
            }
        }


    }

    GridLayout {
        anchors {top: header.bottom; left: parent.left; right: parent.right; bottom: createbutton.top; margins: 15;}
        columns: 2;
        rowSpacing: 15;

        Text {text: "Name:"; MouseArea {anchors.fill: parent; cursorShape: Qt.WhatsThisCursor; onClicked: help.show(help.texts.name);}}
        TextField {
            id: namefield;
            Layout.fillWidth: true;
            onTextChanged: torrent.setName(text);
        }

        Text {text: "Announce url(s):"; MouseArea {anchors.fill: parent; cursorShape: Qt.WhatsThisCursor; onClicked: help.show(help.texts.announce);}}
        TextArea {
            id: announce;
            Layout.preferredHeight: 40;
            Layout.fillWidth: true;
            Layout.fillHeight: true;
            onTextChanged: {torrent.setAnnounceUrls(sets.removeDupes(text).split('\n'), announcemultitier); root.updateUI();}

            ComboBox {
                anchors {right: parent.right; top: parent.top; rightMargin: 15;}
                model: ("\n" + announceurls).split("\n");
                width: 20;
                visible: announceurls.length;
                onActivated: parent.append(textAt(index));
            }
        }

        Text {text: "Webseed url(s):"; MouseArea {anchors.fill: parent; cursorShape: Qt.WhatsThisCursor; onClicked: help.show(help.texts.webseed);}}
        TextArea {
            id: webseed;
            Layout.preferredHeight: 40;
            Layout.fillWidth: true;
            onTextChanged: {torrent.setWebseedUrls(text ? sets.removeDupes(text).split('\n') : ""); root.updateUI();}

            ComboBox {
                anchors {right: parent.right; top: parent.top; rightMargin: 15;}
                model: ("\n" + webseedurls).split("\n");
                width: 20;
                visible: webseedurls.length;
                currentIndex: -1;
                onActivated: parent.append(textAt(index));
            }
        }

        Text {text: "Comment:"; MouseArea {anchors.fill: parent; cursorShape: Qt.WhatsThisCursor; onClicked: help.show(help.texts.comment);}}
        TextField {
            id: commentfield;
            Layout.fillWidth: true;
            onTextChanged: {torrent.setComment(text); root.updateUI();}
        }

        Text {text: "Additional data:"; MouseArea {anchors.fill: parent; cursorShape: Qt.WhatsThisCursor; onClicked: help.show(help.texts.additional);}}
        TextArea {
            Layout.preferredHeight: 40;
            Layout.fillWidth: true;
            onTextChanged: {
                var l = text.split('\n');
                for (var i = 0; i < l.length; i++)
                {
                    var val = l[i].split('=');
                    if (val.length >= 2 && val[0] && val[1])
                        torrent.addAdditionalData(val[0], val[1]);
                }
            }
        }

        Text {text: "Piece length:"; MouseArea {anchors.fill: parent; cursorShape: Qt.WhatsThisCursor; onClicked: help.show(help.texts.piecelength);}}
        ComboBox {
            id: piecelengthcombo;
            model: ["Automatic", "16 KiB", "32 KiB", "64 KiB", "128 KiB", "256 KiB", "512 KiB", "1 MiB", "2 MiB", "4 MiB", "8 MiB", "16 MiB", "32 MiB", "64 MiB"];
            onCurrentIndexChanged: {
                if (currentIndex === -1) return;
                if (currentIndex === 0)
                    torrent.setAutomaticPieceLength();
                else
                    torrent.setPieceLength(textAt(currentIndex).substring(textAt(currentIndex).length -3) === "KiB" ? parseInt(textAt(currentIndex)) * 1024 : parseInt(textAt(currentIndex)) * 1024 * 1024);
                root.updateUI();
            }
        }

        Text {text: "Private:"; MouseArea {anchors.fill: parent; cursorShape: Qt.WhatsThisCursor; onClicked: help.show(help.texts.private);}}
        CheckBox {
            id: privatecheck;
            text: "Private (No DHT / PeX)";
            onCheckedChanged: {torrent.setPrivate(checked); root.updateUI();}
        }

        Rectangle {
            height: 2;
            color: "grey";
            Layout.columnSpan: parent.columns;
            Layout.fillWidth: true;
        }

        Text {text: "Save path:"; MouseArea {anchors.fill: parent; cursorShape: Qt.WhatsThisCursor; onClicked: help.show(help.texts.savepath);}}
        TextField {
            id: savepathedit;
            Layout.fillWidth: true;

            Button {
                anchors {top: parent.top; right: parent.right; bottom: parent.bottom; margins: 2;}
                width: 20;
                text: "...";
                FileDialog {
                    id: savepathdiag;
                    selectExisting: false;
                    nameFilters: ["Torrent file (*.torrent)"];
                    onAccepted: {
                        var path = fileUrl.toString().substring(7);
                        if (path.substring(path.length - 8) !== ".torrent")
                            path += ".torrent";
                        savepathedit.text = path;
                    }
                }
                onClicked: savepathdiag.open();
            }
        }
    }

    CheckBox {
        id: savesettingscheck;
        anchors {left: parent.left; right: createbutton.left; verticalCenter: createbutton.verticalCenter; margins: 15;}
        text: "Save settings";
        checked: true;
    }

    Button {
        id: createbutton;
        enabled: !busy;
        anchors {bottom: footer.top; horizontalCenter: parent.horizontalCenter; margins: 10;}
        text: "  Create";
        height: 35;
        width: parent.width /2;
        iconSource: "qrc:/images/create.png";
        onClicked: {
            announceurls = sets.removeDupes(announceurls +"\n"+ announce.text);
            webseedurls = sets.removeDupes(webseedurls +"\n"+ webseed.text);
            torrent.setCreatedBy("https://github.com/travenn/stc");
            torrent.setCreationDate(new Date().getTime() / 1000);
            torrent.toVariant();
            if (savepathedit.text && torrent.create(savepathedit.text))
            {
                etatimer.elapsed = 0;
                etatimer.start();
                lastsavepath = savepathedit.text.replace(/[^\/]*$/, "");
                busy = true;
            }
            else {
                findiag.title = "Error";
                findiag.text = "Could not create torrent, please make sure all required fields are set!";
                findiag.open();
            }
        }
    }

    RowLayout {
        id: footer;
        anchors {left: parent.left; right: parent.right; bottom: parent.bottom;}
        height: 25;
        spacing: 5;

        Rectangle {
            implicitHeight: parent.height;
            implicitWidth: footertext0.contentWidth + 15;
            Layout.alignment: Qt.AlignCenter;
            border.width: 1;
            border.color: "grey"
            radius: 3;
            Text {id: footertext0; anchors {centerIn: parent;}}
        }
        Rectangle {
            implicitHeight: parent.height;
            implicitWidth: footertext1.contentWidth + 15;
            Layout.alignment: Qt.AlignCenter;
            border.width: 1;
            border.color: "grey"
            radius: 3;
            Text {id: footertext1; anchors {centerIn: parent;}}
        }
        Rectangle {
            implicitHeight: parent.height;
            implicitWidth: footertext2.contentWidth + 15;
            Layout.alignment: Qt.AlignCenter;
            border.width: 1;
            border.color: "grey"
            radius: 3;
            Text {id: footertext2; anchors {centerIn: parent;}}
        }

        ProgressBar {
            id: progressbar;
            Layout.alignment: Qt.AlignCenter;
            Layout.fillWidth: true;
            height: parent.height;
            minimumValue: 0;
            maximumValue: 100;
            value: 0;
            Text {anchors.centerIn: parent; id: progresstext;}
        }
    }

    Component.onCompleted: {
        announceurls = sets.value("STC/Announces", "");
        webseedurls = sets.value("STC/Webseeds", "");
        privatecheck.checked = sets.value("STC/Private", false);
        lastsavepath = sets.value("STC/Lastsavepath", "");
        announcemultitier = sets.value("STC/Announcemultitier", true);
    }

    Component.onDestruction: {
        if (savesettingscheck.checked)
        {
            sets.setValue("STC/Announces", announceurls);
            sets.setValue("STC/Webseeds", webseedurls);
            sets.setValue("STC/Private", privatecheck.checked);
            sets.setValue("STC/Lastsavepath", lastsavepath);
        }
    }
}
