#include "torrentfile.h"

QHash<QString, TorrentFile::DATATYPE> TorrentFile::standardkeys{
    {"pieces", TorrentFile::MINIMAL},
    {"info", TorrentFile::MINIMAL},
    {"announce", TorrentFile::MINIMAL},
    {"piece length", TorrentFile::MINIMAL},
    {"name", TorrentFile::MINIMAL},
    {"length", TorrentFile::MINIMAL},
    {"files", TorrentFile::MINIMAL},
    {"path", TorrentFile::MINIMAL},
    {"announce-list", TorrentFile::STANDARD},
    {"creation date", TorrentFile::STANDARD},
    {"comment", TorrentFile::STANDARD},
    {"created by", TorrentFile::STANDARD},
    {"private", TorrentFile::STANDARD},
    {"encoding", TorrentFile::STANDARD}
};

TorrentFile::TorrentFile(QObject *parent) : QObject(parent)
{
    m_data = QVariantMap{{"info", QVariantMap()}};
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &TorrentFile::onWatchedDirChanged);
}

TorrentFile::TorrentFile(const QVariant &data, QObject *parent) : QObject(parent)
{
    m_data = data.toMap();
}

TorrentFile::TorrentFile(const QString &filename, DATATYPE keytype, QObject *parent) : QObject(parent)
{
    load(filename, keytype);
}

TorrentFile::~TorrentFile()
{
    if (m_hashthread)
    {
        m_hasher->abort();
        m_hashthread->quit();
        m_hashthread->wait();
    }
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
}

bool TorrentFile::load(const QString &filename, DATATYPE keytype)
{
    resetFiles();

    QFile f(filename);
    if (f.open(QIODevice::ReadOnly))
    {
        m_data = decodeBencode(f.readAll(), keytype).toMap();
        m_realname = m_data.value("info").toMap().value("name").toString();
        f.close();
        return true;
    }
    return false;
}

bool TorrentFile::create(const QString &filename)
{
    if (m_hashthread)
        return false;

    if (!getPieceLength())
        setAutomaticPieceLength();

    m_savetorrentfilename = filename;

    m_hasher = new TorrentFileHasher(m_filelist, getPieceLength(), getContentLength());
    m_hashthread = new QThread(this);
    m_hasher->moveToThread(m_hashthread);
    connect(m_hasher, &TorrentFileHasher::progressUpdate, this, &TorrentFile::progress, Qt::QueuedConnection);
    connect(m_hasher, &TorrentFileHasher::done, this, &TorrentFile::onThreadFinished, Qt::QueuedConnection);
    connect(m_hasher, &TorrentFileHasher::error, this, &TorrentFile::error);
    connect(m_hashthread, &QThread::started, m_hasher, &TorrentFileHasher::hash, Qt::QueuedConnection);
    connect(m_hashthread, &QThread::finished, m_hasher, &TorrentFileHasher::deleteLater, Qt::QueuedConnection);
    m_hashthread->start();

    return true;
}

qint64 TorrentFile::calculateTorrentfileSize()
{
    if (!getPieceLength())
        setAutomaticPieceLength();

    QVariantMap map = m_data;
    QVariantMap m = map.value("info").toMap();
    m.remove("pieces");
    map.insert("info", m);

    QByteArray encoded = encode(map);
    qint64 pbytes = getPieceNumber() * 20;
    return encoded.size() + 8 /*6:pieces*/ + QByteArray::number(pbytes).size() +1 /*<bytenumber>:*/ + pbytes ;
}

qint64 TorrentFile::getPieceNumber()
{
    if (!getContentLength() || !getPieceLength())
        return 0;
    return getContentLength() % getPieceLength() ? getContentLength() / getPieceLength() +1 : getContentLength() / getPieceLength();
}

void TorrentFile::setFile(const QString &filename)
{
    resetFiles();

    QVariantMap m = m_data.value("info").toMap();
    m.remove("files");
    QFileInfo f(filename);
    m_filelist.insert(filename, f.size());
    m.insert("name", f.fileName());
    m_realname = f.fileName();
    m.insert("length", f.size());
    m_data.insert("info", m);
    m_parentdir = f.absolutePath();
    if (!m_parentdir.endsWith('/'))
        m_parentdir += "/";
}

void TorrentFile::setDirectory(const QString &path)
{
    resetFiles();
    m_parentdir = path.left(path.lastIndexOf('/', -2) +1);

    QVariantMap m = m_data.value("info").toMap();
    m.remove("length");
    QDir dir(path);
    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    m.insert("name", dir.dirName());
    m_realname = dir.dirName();
    QVariantList files;

    QDirIterator it(dir, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        qint64 length = it.fileInfo().size();
        QString name = it.filePath();
        m_filelist.insert(name, length);
        files.append(QVariantMap{{"length", length}, {"path", dir.relativeFilePath(name).split('/')}});
    }
    m.insert("files", files);
    m_data.insert("info", m);

    QStringList watchdirs(path);
    QDirIterator dirsit(path, QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while(dirsit.hasNext())
    {
        dirsit.next();
        watchdirs << dirsit.filePath();
    }
    m_watcher.addPaths(watchdirs);
}

void TorrentFile::setRootDirectory(const QString &path)
{
    m_parentdir = path;
    if (!m_parentdir.endsWith('/'))
        m_parentdir += "/";
    resetFiles();

    QVariantList files = m_data.value("info").toMap().value("files").toList();
    if (files.isEmpty())
        m_filelist.insert(m_parentdir + m_data.value("info").toMap().value("name").toString(), m_data.value("info").toMap().value("length", 0).toLongLong());
    else
    {
        QString root = m_parentdir + m_data.value("info").toMap().value("name").toString() + "/";
        for (auto i = files.constBegin(); i != files.constEnd(); ++i)
        {
            QVariantMap m = (*i).toMap();
            QString path = root + m.value("path").toStringList().join("/");
            qint64 length = m.value("length").toLongLong();
            m_filelist.insert(path, length);
        }

        QStringList watchdirs(root);
        QDirIterator dirsit(root, QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while(dirsit.hasNext())
        {
            dirsit.next();
            watchdirs << dirsit.filePath();
        }
        m_watcher.addPaths(watchdirs);
    }

}

void TorrentFile::abortHashing()
{
    if (m_hashthread)
    {
        m_hasher->abort();
        m_hashthread->quit();
        m_hashthread->wait();
        m_hashthread->deleteLater();
        m_hashthread = 0;
    }
}

QStringList TorrentFile::getAnnounceUrls() const
{
    QVariantList vl = m_data.value("announce-list").toList();
    QStringList l;
    QString a = m_data.value("announce").toString();
    if (!a.isEmpty())
        l << a;
    for (auto i = vl.constBegin(); i != vl.constEnd(); ++i)
        l << (*i).toStringList();
    l.removeDuplicates();
    return l;
}

qint64 TorrentFile::getContentLength() const
{
    qint64 ret = 0;
    if (m_filelist.isEmpty())
    {
        QVariantList files = m_data.value("info").toMap().value("files").toList();
        if (files.isEmpty())
            ret = m_data.value("info").toMap().value("length", 0).toLongLong();
        else
            for (auto i = files.constBegin(); i != files.constEnd(); ++i)
                ret += (*i).toMap().value("length", 0).toLongLong();
    }
    else
        for (auto i = m_filelist.constBegin(); i != m_filelist.constEnd(); ++i)
            ret += i.value();
    return ret;
}

QVariant TorrentFile::getAdditionalData() const
{
    QVariantMap v = m_data;
    QVariantMap info = m_data.value("info").toMap();
    QVariantList files = info.value("files").toList();
    QVariantList filesnew;

    for (auto i = files.constBegin(); i != files.constEnd(); ++i)
    {
        QVariantMap fmap = (*i).toMap();
        if (fmap.size() > 2)
        {
            fmap.remove("path");
            fmap.remove("length");
            filesnew.append(fmap);
        }
    }


    QStringList stdk = standardkeys.keys();
    for (auto x = stdk.constBegin(); x != stdk.constEnd(); ++x)
    {
        info.remove((*x));
        v.remove((*x));
    }
    if (!filesnew.isEmpty())
        info.insert("files", filesnew);
    if (!info.isEmpty())
        v.insert("info", info);

    return v.isEmpty() ? QVariant() : v;
}

void TorrentFile::setName(const QString& name)
{
    QVariantMap m = m_data.value("info").toMap();
    m.insert("name", name);
    m_data.insert("info", m);
}

void TorrentFile::setAnnounceUrl(const QString &url)
{
    m_data.remove("announce-list");
    m_data.insert("announce", url);
}

void TorrentFile::setAnnounceUrls(const QStringList &list, const bool &multitier)
{
    m_data.remove("announce-list");
    m_data.remove("announce");
    if (list.isEmpty())
        return;

    m_data.insert("announce", list.first());
    if (list.size() == 1)
        return;

    QVariantList vlist;
    for (auto i = list.constBegin(); i != list.constEnd(); ++i)
    {
        if (multitier)
            vlist << QVariantList{QVariantList{(*i)}};
        else
            vlist << (*i);
    }
    if (!multitier)
        vlist = QVariantList{vlist};

    m_data.insert("announce-list", vlist);
}

void TorrentFile::setAnnounceUrls(const QVariantList& list)
{
    m_data.insert("announce", list.first().toList().first());
    m_data.insert("announce-list", list);
}

void TorrentFile::setWebseedUrls(const QStringList &list)
{
    m_data.remove("url-list");
    if (list.isEmpty())
        return;
    if (list.size() == 1)
        m_data.insert("url-list", list.first());
    else
    {
        QVariantList vlist;
        for (auto i = list.constBegin(); i != list.constEnd(); ++i)
            vlist << (*i);
        m_data.insert("url-list", vlist);
    }
}

void TorrentFile::setPieceLength(const qint64 &bytes)
{
    QVariantMap m = m_data.value("info").toMap();
    m.insert("piece length", bytes);
    m_data.insert("info", m);
}

void TorrentFile::setPrivate(const bool &is_private)
{
    QVariantMap m = m_data.value("info").toMap();
    m.remove("private");
    if (is_private)
        m.insert("private", is_private);
    m_data.insert("info", m);
}

QVariant TorrentFile::decodeBencode(const QByteArray& bencode, DATATYPE keytype, qint64* parsedLength)
{
    bool islist = bencode.at(0) == 'l' ? 1 : 0;
    qint64 pos = 1;
    QVariant ret;
    QVariantMap cmap;
    QVariantList clist;
    QString key;

    while (pos < bencode.size())
    {
        QChar typechar(bencode.at(pos));
        if (typechar.isDigit())
        {
            int idx = bencode.indexOf(':', pos);
            QByteArray ls = bencode.mid(pos, idx - pos);
            int length = ls.toInt();
            QByteArray s = bencode.mid(pos + ls.length() +1, length);
            if (islist)
                clist.append(s);
            else
            {
                if (key.isEmpty())
                    key = s;
                else
                {
                    if (standardkeys.value(key, ADDITIONAL) <= keytype)
                        cmap.insert(key, s);
                    key = "";
                }
            }
            pos += ls.length() + 1 + length;
            continue;
        }

        if (typechar == 'i')
        {
            QByteArray s = bencode.mid(pos +1, bencode.indexOf('e', pos) - pos -1);
            qint64 value = s.toLongLong();
            if (islist)
                clist.append(value);
            else
            {
                if (standardkeys.value(key, ADDITIONAL) <= keytype)
                    cmap.insert(key, value);
                key = "";
            }
            pos += s.length() + 2;
            continue;
        }

        if (typechar == 'l' || typechar == 'd')
        {
            QByteArray s = bencode.mid(pos);
            qint64 pl = 0;
            QVariant v = decodeBencode(s, keytype, &pl);
            if (islist)
                clist.append(v);
            else
            {
                if (key == "info")
                    m_infohash = QCryptographicHash::hash(bencode.mid(pos, pl), QCryptographicHash::Sha1);
                if (standardkeys.value(key, ADDITIONAL) <= keytype)
                    cmap.insert(key, v);
                key = "";
            }
            pos += pl;
            continue;
        }

        if (bencode.at(pos) == 'e')
            break;

        break;
    }

    if (parsedLength)
        *parsedLength = pos +1;

    if (islist)
        ret = clist;
    else
        ret = cmap;
    return ret;
}

QByteArray TorrentFile::encode(const QVariant &data, const bool createinfohash)
{
    switch (data.type())
    {
        default:
        case QMetaType::QString:
        case QMetaType::QByteArray: return QByteArray::number(data.toByteArray().length()) + ":" + data.toByteArray();
        case QMetaType::Bool: return QString("i%1e").arg(data.toInt()).toUtf8();
        case QMetaType::Int:
        case QMetaType::LongLong: return QString("i%1e").arg(data.toString()).toUtf8();
        case QMetaType::QVariantMap:
        {
            QByteArray ret = "d";
            for (auto i = data.toMap().constBegin(); i != data.toMap().constEnd(); ++i)
            {
                QByteArray val = encode(i.value(), createinfohash);
                if (i.key() == "info" && createinfohash)
                    m_infohash = QCryptographicHash::hash(val, QCryptographicHash::Sha1);
                ret += encode(i.key(), createinfohash) + val;
            }
            ret += "e";
            return ret;
        }
        case QMetaType::QStringList:
        {
            QStringList l = data.toStringList();
            QByteArray ret = "l";
            for (auto i = l.constBegin(); i != l.constEnd(); ++i)
                ret += encode(*i, createinfohash);
            ret += "e";
            return ret;
        }
        case QMetaType::QVariantList:
        {
            QByteArray ret = "l";
            for (auto i = data.toList().constBegin(); i != data.toList().constEnd(); ++i)
                ret += encode(*i, createinfohash);
            ret += "e";
            return ret;
        }
    }
}

void TorrentFile::resetFiles()
{
    m_filelist.clear();
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
}

void TorrentFile::onWatchedDirChanged(const QString &dir)
{
    if (m_hashthread)
    {
        abortHashing();
        emit error("Added / removed files in " + dir + ". Operation aborted!");
    }
    else
        emit watchedDirChanged(dir);
}

qint64 TorrentFile::setAutomaticPieceLength()
{
    qint64 contentsize = getContentLength();
    qint64 piecesize = 16*1024;
    qint64 piecenum = (contentsize / piecesize) +1;
    qint64 maxpsize = 16 *1024 *1024;
    int maxpiecenmb = 2000;
    while (piecesize < maxpsize && piecenum > maxpiecenmb)
    {
        piecesize *= 2;
        piecenum = (contentsize / piecesize) +1;
        switch (piecesize)
        {
            case 256 * 1024: maxpiecenmb = 5000; break;
            case 1 *1024 *1024: maxpiecenmb = 10000; break;
            case 4 *1024 *1024: maxpiecenmb = 15000; break;
            case 8 *1024 *1024: maxpiecenmb = 20000; break;
        }
    }
    QVariantMap m = m_data.value("info").toMap();
    m.insert("piece length", piecesize);
    m_data.insert("info", m);
    return piecesize;
}

void TorrentFile::dupe()
{
    QVariantMap m = m_data.value("info").toMap();
    m.insert("duped", QDateTime::currentMSecsSinceEpoch() / 1000);
    m_data.insert("info", m);
}

void TorrentFile::onThreadFinished(QByteArray pieces)
{
    if (m_hashthread)
    {
        m_hashthread->quit();
        m_hashthread->wait(5000);
        m_hashthread->deleteLater();
        m_hashthread = 0;
    }
    QVariantMap m = m_data.value("info").toMap();
    m.insert("pieces", pieces);
    m_data.insert("info", m);
    if (m_savetorrentfilename.isEmpty())
        emit finished(true);
    QFile f(m_savetorrentfilename);
    if (f.open(QIODevice::ReadWrite | QIODevice::Truncate))
    {
        f.write(encode(m_data, 1));
        f.close();
        emit finished(true);
    }
    else
        emit finished(false);
}



