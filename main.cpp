#include <QApplication>
#include <QQuickWidget>
#include <QQmlEngine>
#include <QQmlContext>
#include <QDateTime>
#include <QCommandLineParser>
#include <QTextStream>
#include <QJsonDocument>

#include "torrentfile.h"
#include "qmlsettings.h"


QString prettySize(const qint64& bytes)
{
    double res = bytes;
    QStringList l = QStringList() << "Bytes" << "KiB" << "MiB" << "GiB" << "TiB" << "PiB" << "EiB";
    int i = 0;
    while(res >= 1024)
    {
        ++i;
        res /= 1024.0;
    }
    return QString("%1 %2").arg(res, 0, 'f', 2).arg(l.at(i)).replace(".00", "");
}

int main(int argc, char *argv[])
{

    TorrentFile t;
    t.setCreationDate(QDateTime::currentMSecsSinceEpoch() / 1000);
    t.setCreatedBy("https://github.com/travenn/stc");

    bool gui = false;
    if (argc < 3)
    {
        gui = true;
        QStringList l = QStringList() << "-h" << "--help" << "-i" << "--inspect";
        for (int i = 0; i < argc; ++i)
            if (l.contains(argv[i]))
                gui = false;
    }


    if (!gui)
    {
        QCoreApplication app(argc, argv);
        QCommandLineParser p;
        p.addHelpOption();
        p.setApplicationDescription("[S]imple [T]orrent [C]reator");
        p.addPositionalArgument("announce", "The announce url. You can add more urls using -a option.");
        p.addPositionalArgument("source", "The path to a file or directory you want to create a torrent from.");
        p.addPositionalArgument("target", "The path where to save the metainfo (.torrent) file.");
        p.addOptions({
                         {{"a", "announce"}, "Announce url. Can be used multiple times.", "announce"},
                         {{"c", "comment"}, "Torrent comment", "comment"},
                         {{"d", "data"}, "You can set any additional key value pair. Key and value must be seperated with a '=' e.g.: '-d mykey1=myvalue1 -d mykey2=myvalue2'.", "data"},
                         {{"i", "inspect"}, "Prints content of specified torrent in JSON.", "torrentfile"},
                         {{"n", "name"}, "Sets an alternate name.", "name"},
                         {{"o", "overwrite"}, "Overwrite existing metainfo file without asking."},
                         {{"p", "private"}, "Sets the torrent private."},
                         {{"s", "l", "size", "length"}, "Piece length in bytes. You can append a 'k' for KiB or 'm' for MiB e.g.: '64k' for '65536'. Any value < 16k will be interpreted as a maximum piece number for autogeneration. E.g.: '-l 3500' would choose a piece length that results in less than 3501 pieces. Exception: Autogeneration won't create a length > 16MiB to ensure client compatibility.", "size"},
                         {{"t", "simulate"}, "Doesn't hash or create a metafile. Can be used to calculate the piece length, number of pieces and the metainfo size before creating."},
                         {{"v", "verbose"}, "Prints JSON representation."},
                         {{"w", "webseed"}, "Webseed url. Can be used multiple times.", "webseedurl"},
                     });
        p.process(app);
        QTextStream out(stdout);

        if (p.isSet("inspect"))
        {
            QVariantMap m = TorrentFile(p.value("inspect")).toVariant().toMap();
            QVariantMap info = m.value("info").toMap();
            info.insert("pieces", "<stripped>");
            m.insert("info", info);
            out << QJsonDocument::fromVariant(m).toJson() << endl;
            return 0;
        }

        QStringList positionals = p.positionalArguments();
        if (positionals.size() != 3)
            p.showHelp(1);
        QString announce = positionals.at(0);
        QString source = positionals.at(1);
        QString target = positionals.at(2);


        if (QFileInfo(source).isDir())
            t.setDirectory(source);
        else
            t.setFile(source);

        t.setAnnounceUrls(p.values("announce") << announce);
        t.setComment(p.value("comment"));
        QStringList additional = p.values("data");
        for (auto i = additional.constBegin(); i != additional.constEnd(); ++i)
            t.addAdditionalData((*i).split('=').first(), (*i).split('=').at(1));
        if (p.isSet("name"))
            t.setName(p.value("name"));
        if (p.isSet("private"))
            t.setPrivate(true);
        QString plength = p.value("length");
        if (!plength.isEmpty())
        {
            qint64 length = plength.toLongLong();
            if (plength.endsWith('k', Qt::CaseInsensitive))
                length = plength.left(plength.length() -1).toLongLong() * 1024;
            if (plength.endsWith('m', Qt::CaseInsensitive))
                length = plength.left(plength.length() -1).toLongLong() * 1024 * 1024;
            if (!length)
                t.setAutomaticPieceSize(t.getContentLength());
            else if (length < 16 * 1024)
                t.setAutomaticPieceSize(t.getContentLength(), length);
            else
                t.setPieceSize(length);
        }
        else
            t.setAutomaticPieceSize(t.getContentLength());
        t.setWebseedUrls(p.values("webseed"));

        if (p.isSet("verbose"))
            out << QJsonDocument::fromVariant(t.toVariant()).toJson() << endl;

        out << "Total size: " << prettySize(t.getContentLength()) << endl;
        out << "Piece length: " << prettySize(t.getPieceSize()) << endl;
        out << "Number of pieces: " << t.getPieceNumber() << endl;
        out << "Metainfo size: " << prettySize(t.calculateTorrentfileSize()) << endl;

        if (p.isSet("simulate"))
            return 0;


        if (QFile::exists(target) && !p.isSet("overwrite"))
        {
            QTextStream in(stdin);
            out << target << " already exists, overwrite? [Y]es / [N]o" << endl;
            QString c;
            in >> c;
            if (QString::compare(c, "y", Qt::CaseInsensitive))
                return 0;
        }


        qint64 starttime = QDateTime::currentMSecsSinceEpoch();
        QObject::connect(&t, &TorrentFile::progress, [&] (int p)
        {
            qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - starttime;
            qint64 eta = (((100 / (double)p) * elapsed) - elapsed) / 1000;
            int h = eta / 3600;
            eta = eta % 3600;
            int m = eta / 60;
            int s = eta % 60;
            out << QString("\r%1%  ETA: %2:%3:%4").arg(p).arg(h,2,10,QChar('0')).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0')) << flush;
        });
        QObject::connect(&t, &TorrentFile::finished, [&] (bool s)
        {
            out << endl;
            if (!s)
                out << "Something went wrong, operation failed!" << endl;
            app.quit();
        });

        if (!t.create(target))
        {
            out << "Files not found." << endl;
            return 1;
        }
        return app.exec();
    }

    else
    {
        QApplication app(argc, argv);

        QmlSettings s;
        QQuickWidget w;
        w.setWindowIcon(QIcon(QPixmap(":/images/dir.png")));
        w.engine()->rootContext()->setContextProperty("torrent", &t);
        w.engine()->rootContext()->setContextProperty("sets", &s);
        w.setResizeMode(QQuickWidget::SizeRootObjectToView);
        w.setSource(QUrl("qrc:/main.qml"));
        w.show();
        return app.exec();
    }
}
