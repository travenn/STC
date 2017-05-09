#include <QApplication>
#include <QQuickWidget>
#include <QQmlEngine>
#include <QQmlContext>
#include <QDateTime>
#include <QCommandLineParser>
#include <QTextStream>
#include <QJsonDocument>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "torrentfile.h"
#include "qmlsettings.h"

#define APPNAME "Simple Torrent Creator"
#define VERSION "0.0.9"

QTextStream out(stdout);
void quit(const int exitcode = 0)
{
    out << endl;
#ifdef Q_OS_WIN
    out << QDir::toNativeSeparators(QDir::currentPath()) + ">" << flush;
#endif
    exit(exitcode);
}

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
    bool gui = false;
    if (argc < 3)
    {
        gui = true;
        QStringList l = QStringList() << "-h" << "--help" << "-i" << "--inspect" << "--dupe" << "--hashcompare";
        for (int i = 0; i < argc; ++i)
            if (l.contains(argv[i]))
                gui = false;
    }


    if (!gui)
    {
#ifdef Q_OS_WIN
        FreeConsole();
        AttachConsole(ATTACH_PARENT_PROCESS);
        freopen("CON", "w", stdout);
        freopen("CON", "w", stderr);
        freopen("CON", "r", stdin);
#endif
        QCoreApplication app(argc, argv);
        TorrentFile t;
        t.setCreationDate(QDateTime::currentMSecsSinceEpoch() / 1000);
        t.setCreatedBy(QString("%1 %2").arg(APPNAME, VERSION));

        QCommandLineParser p;
        p.addHelpOption();
        p.setApplicationDescription("[S]imple [T]orrent [C]reator");
        p.addPositionalArgument("announce", "The announce url. You can add more urls using -a option.");
        p.addPositionalArgument("source", "The path to a file or directory you want to create a torrent from.");
        p.addPositionalArgument("target", "The path where to save the metainfo (.torrent) file.");
        p.addOptions({
                         {{"a", "announce"}, "Adds a announce url. Can be used multiple times.", "announce"},
                         {{"c", "comment"}, "Sets the torrents comment to <comment>", "comment"},
                         {{"d", "data"}, "You can set any additional key value pair. Key and value must be seperated with a '=' e.g.: '-d mykey1=myvalue1 -d mykey2=myvalue2'.", "data"},
                         {"dupe", "Usage: \"stc --dupe <original.torrent> <announce> <new.torrent>\".Creates a duplicate of the original having a different torrent hash without rehashing the files.\nThis can be used to seed the same data to multiple private trackers.\nAltering the hash avoids \"illegal cross-seeding\".\nYou can also change everything not file related (urls, private etc.)."},
                         {"hashcompare", "Usage: \"stc --hashcompare <torrentfile1> <torrentfile2> [<torrentfileX>]...\".\nIf used without -v just prints 0(not the same) or 1(equal). The return code will also reflect this."},
                         {{"i", "inspect"}, "Prints information about the torrentfile. If -v is set outputs JSON representation.", "torrentfile"},
                         {{"n", "name"}, "Sets an alternate name.", "name"},
                         {{"o", "overwrite"}, "Overwrite existing metainfo file without asking. Implicitly set on windows."},
                         {{"p", "private"}, "Sets the torrents private flag."},
                         {{"r", "randomhash"}, "Creates the torrent with a random piece hash (useful for some file based duplicate checkers)."},
                         {{"s", "l", "size", "length"}, "Piece length in bytes. You can append a 'k' for KiB or 'm' for MiB e.g.: '-l512k' for 524288 bytes.", "size"},
                         {{"t", "simulate"}, "Doesn't hash or create a metafile. Can be used to calculate the piece length, number of pieces and the metainfo size before creating."},
                         {{"v", "verbose"}, "Prints additional information dependent on the other options used."},
                         {{"w", "webseed"}, "Webseed url. Can be used multiple times.", "webseedurl"},
                     });
        p.process(app);
        bool verbose = p.isSet("verbose");

        if (p.isSet("hashcompare"))
        {
            QStringList positionals = p.positionalArguments();
            if (positionals.size() < 2)
                p.showHelp(0);

            QSet<QByteArray> hashes;
            for (auto i = positionals.constBegin(); i != positionals.constEnd(); ++i)
            {
                TorrentFile tf((*i));
                hashes << tf.getInfoHash();
                if (verbose)
                    out << (*i) << ": " << tf.getInfoHash(true) << endl;
            }
            if (hashes.size() != 1)
            {
                if (verbose) out << "Hashes don't match." << endl;
                else out << "0";
                quit();
            }
            else
            {
                if (verbose) out << "Hashes are the same." << endl;
                else out << "1";
                quit(!verbose);
            }
        }
        out << endl;


        if (p.isSet("dupe"))
        {
            QStringList positionals = p.positionalArguments();
            if (positionals.size() != 3)
                p.showHelp(0);

            QString source = positionals.at(0);
            QString announce = positionals.at(1);
            QString target = positionals.at(2);

            if (!t.load(source, TorrentFile::ADDITIONAL))
            {
                out << "Can't find " << source << endl;
                quit(1);
            }
            QByteArray ohash = t.getInfoHash();
            if (verbose)
            {
                QVariantMap m = t.toVariant().toMap();
                QVariantMap info = m.value("info").toMap();
                info.insert("pieces", "<stripped>");
                m.insert("info", info);
                out << QJsonDocument::fromVariant(m).toJson() << endl;
            }
            out << "Original hash: " << ohash.toHex() << endl;

            t.setAnnounceUrls(QStringList() << announce << p.values("announce"));
            if (p.isSet("webseed"))
                t.setWebseedUrls(p.values("webseed") << t.getWebseedUrls());
            if (p.isSet("private"))
                t.setPrivate(!t.isPrivate());
            t.setCreationDate(QDateTime::currentMSecsSinceEpoch() / 1000);
            t.setCreatedBy(QString("%1 %2").arg(APPNAME, VERSION));
            QByteArray bcode;
            while (ohash == t.getInfoHash())
            {
                t.dupe();
                bcode = t.encode(t.toVariant().toMap(), true);
            }
            out << "Duplicate hash: " << t.getInfoHash(true) << endl;

#ifndef Q_OS_WIN
            if (QFile::exists(target) && !p.isSet("overwrite"))
            {
                QTextStream in(stdin);
                out << target << " already exists, overwrite? [Y]es / [N]o" << endl;
                QString c;
                in >> c;
                if (QString::compare(c, "y", Qt::CaseInsensitive))
                    quit();
            }
#endif
            QFile f(target);
            if (!f.open(QIODevice::ReadWrite | QIODevice::Truncate) || f.write(bcode) == -1)
            {
                out << "Can't write file: " << target << endl;
                quit(1);
            }
            f.close();
            quit();

        }

        if (p.isSet("inspect"))
        {
            t.load(p.value("inspect"));

            if (verbose)
            {
                QVariantMap m = t.toVariant().toMap();
                QVariantMap info = m.value("info").toMap();
                info.insert("pieces", "<stripped>");
                m.insert("info", info);
                out << QJsonDocument::fromVariant(m).toJson() << endl;
            }
            else
            {
                out << "Name: " << t.getName() << endl;
                out << "Announce urls: " << t.getAnnounceUrls().join(", ") << endl;
                if (!t.getWebseedUrls().isEmpty())
                    out << "Webseed urls: " << t.getWebseedUrls().join(", ") << endl;
                if (!t.getCreatedBy().isEmpty())
                    out << "Created by: " << t.getCreatedBy() << endl;
                if (t.getCreationDate())
                    out << "Creation date: " << QDateTime::fromTime_t(t.getCreationDate(), Qt::UTC).toString(Qt::ISODate) << endl;
                if (!t.getComment().isEmpty())
                    out << "Comment: " << t.getComment() << endl;
                if (t.isPrivate())
                    out << "Private: true" << endl;
                out << "Total size: " << prettySize(t.getContentLength()) << endl;
                out << "Piece length: " << prettySize(t.getPieceLength()) << endl;
                out << "Number of pieces: " << t.getPieceNumber() << endl;
                out << "Metainfo size: " << prettySize(t.calculateTorrentfileSize()) << endl;
                out << "Info hash: " << t.getInfoHash(true) << endl;
            }
            quit();
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

        t.setAnnounceUrls(QStringList() << announce << p.values("announce"));
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
                t.setAutomaticPieceLength();
            else
                t.setPieceLength(length);
        }
        else
            t.setAutomaticPieceLength();
        t.setWebseedUrls(p.values("webseed"));

        if (verbose)
            out << QJsonDocument::fromVariant(t.toVariant()).toJson() << endl;

        out << "Total size: " << prettySize(t.getContentLength()) << endl;
        out << "Piece length: " << prettySize(t.getPieceLength()) << endl;
        out << "Number of pieces: " << t.getPieceNumber() << endl;
        out << "Metainfo size: " << prettySize(t.calculateTorrentfileSize()) << endl;

        if (p.isSet("simulate"))
            quit();


#ifndef Q_OS_WIN
        if (QFile::exists(target) && !p.isSet("overwrite"))
        {
            QTextStream in(stdin);
            out << target << " already exists, overwrite? [Y]es / [N]o" << endl;
            QString c;
            in >> c;
            if (QString::compare(c, "y", Qt::CaseInsensitive))
                quit();
        }
#endif

        if (p.isSet("randomhash"))
        {
            out << endl << "Creating torrent with random hash." << endl;
            out << "Warning: This torrent can't be used to transfer any data." << endl;
            out << "Only use it for file based duplicate checkers." << endl;
            QVariantMap map = t.toVariant().toMap();
            QVariantMap m = map.value("info").toMap();
            QByteArray randompieces;
            randompieces.resize(t.getPieceNumber() * 20);
            for(int i = 0; i < randompieces.length(); ++i)
                randompieces[i] = rand();
            m.insert("pieces", randompieces);
            map.insert("info", m);

            QFile f(target);
            if (f.open(QIODevice::WriteOnly))
            {
                f.write(t.encode(map, 1));
                f.close();
                out << endl << "Finished: Info hash: " << t.getInfoHash(true) << endl;
                quit();
            }
            out << endl << "Error: Could not write to " + target << endl;
            quit(1);
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
                out << endl << "Something went wrong, operation failed!" << endl;
            else
                out << endl << "Finished: Info hash: " << t.getInfoHash(true) << endl;
            app.quit();
        });
        QObject::connect(&t, &TorrentFile::error, [&] (QString msg)
        {
            out << endl << msg;
            app.quit();
        });

        if (!t.create(target))
        {
            out << endl << "Files not found." << endl;
            quit();
        }
        else
            out << "0% ETA: -" << flush;
        quit(app.exec());
    }

    else
    {
        QApplication app(argc, argv);
        app.setApplicationName(APPNAME);
        app.setApplicationVersion(VERSION);
        TorrentFile t;
        t.setCreationDate(QDateTime::currentMSecsSinceEpoch() / 1000);
        t.setCreatedBy(QString("%1 %2").arg(APPNAME, VERSION));
        QmlSettings s;
        QQuickWidget w;
        w.setWindowTitle(QString("%1 %2").arg(APPNAME, VERSION));
        w.setWindowIcon(QIcon(":/images/file.ico"));
        w.engine()->rootContext()->setContextProperty("torrent", &t);
        w.engine()->rootContext()->setContextProperty("sets", &s);
        w.setResizeMode(QQuickWidget::SizeRootObjectToView);
        w.setSource(QUrl("qrc:/main.qml"));
        w.show();

        return app.exec();
    }
}
